#include "vm/bcode_asm.h"
#include "vm/bcode.h"
#include "vm/bcode_asm_defs.h"
#include "core/allocator.h"
#include "core/strslice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ================================================================ */
/* 汇编器：.cxs 文本 → bytecode_t                                     */
/* ================================================================ */

typedef struct {
    const char *p;     /* 当前行起始（已去首尾空白） */
    const char *end;   /* 当前行结束（exclusive） */
    size_t      line;  /* 1-based 行号（诊断用） */
    char        err[256];
} asm_parser_t;

/* ---- 标签表与 fixup（支持前向引用回填） ---- */

typedef struct {
    char     name[64];
    uint32_t pc;        /* 标签所在 code 字节偏移（定义处） */
} asm_label_t;

typedef struct {
    size_t   pos;       /* 待回填的 u32 在 code 流中的偏移 */
    size_t   line;      /* 诊断用行号 */
    char     name[64];  /* 引用的标签名 */
} asm_fixup_t;

/* 标签引用字符集：[A-Za-z0-9_.-] */
static bool label_char_ok(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '-';
}

/* ---- 大小写无关比较（长度受限） ---- */

static bool strnieq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

/* ---- 助记符反向查找（表按 opcode 索引，index == opcode；大小写无关） ---- */

static bool lookup_mnemonic(const char *mn, size_t mlen,
                            bcode_op_t *op_out,
                            const bcode_asm_entry_t **entry_out) {
    for (size_t i = 0; i < BCODE_ASM_TABLE_COUNT; i++) {
        const bcode_asm_entry_t *e = &BCODE_ASM_TABLE[i];
        if (!e->mnemonic) continue;
        size_t n = strlen(e->mnemonic);
        if (n == mlen && strnieq(e->mnemonic, mn, mlen)) {
            *op_out = (bcode_op_t)i;
            *entry_out = e;
            return true;
        }
    }
    return false;
}

/* ---- C 字符串字面量解码（"..." 内 \n \t \r \" \\ \xHH） ---- */
/* out==NULL 时仅计算长度并校验（两遍法，避免长度上限假设）。          */

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_cstr(const char **pp, const char *end,
                       uint8_t *out, size_t *out_len) {
    if (*pp >= end || **pp != '"') return -1;
    (*pp)++;
    size_t n = 0;
    while (*pp < end) {
        char c = **pp;
        if (c == '\\') {
            (*pp)++;
            if (*pp >= end) return -1;
            char e = **pp;
            (*pp)++;
            uint8_t val;
            switch (e) {
                case 'n': val = '\n'; break;
                case 't': val = '\t'; break;
                case 'r': val = '\r'; break;
                case '"': val = '"';  break;
                case '\\': val = '\\'; break;
                case 'x': {
                    if (end - *pp < 2) return -1;
                    int hi = hexval((*pp)[0]);
                    int lo = hexval((*pp)[1]);
                    if (hi < 0 || lo < 0) return -1;
                    val = (uint8_t)((hi << 4) | lo);
                    (*pp) += 2;
                    break;
                }
                default:
                    return -1; /* 无效转义 */
            }
            if (out) out[n] = val;
            n++;
        } else if (c == '"') {
            (*pp)++;
            *out_len = n;
            return 0;
        } else {
            if (out) out[n] = (uint8_t)c;
            n++;
            (*pp)++;
        }
    }
    return -1; /* 未闭合 */
}

/* ---- 操作数解析（按操作数类型写入 code 流） ---- */

static int parse_operand(bytecode_t *bc, bcode_asm_operand_t kind,
                         const char *tk, size_t tlen, char *err) {
    char buf[40];
    if (tlen == 0 || tlen >= sizeof buf) {
        snprintf(err, 256, "malformed operand token");
        return -1;
    }
    memcpy(buf, tk, tlen);
    buf[tlen] = '\0';

    char *endp = NULL;
    errno = 0;

    switch (kind) {
        case BCODE_ASM_OP_U32: {
            unsigned long long v = strtoull(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0 || v > 0xFFFFFFFFULL) {
                snprintf(err, 256, "invalid u32 operand '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_u32(bc, (uint32_t)v);
            return 0;
        }
        case BCODE_ASM_OP_I8: {
            long long v = strtoll(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0 ||
                v < -128 || v > 127) {
                snprintf(err, 256, "i8 operand out of range '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_i8(bc, (int8_t)v);
            return 0;
        }
        case BCODE_ASM_OP_I16: {
            long long v = strtoll(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0 ||
                v < -32768 || v > 32767) {
                snprintf(err, 256, "i16 operand out of range '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_i16(bc, (int16_t)v);
            return 0;
        }
        case BCODE_ASM_OP_I32: {
            long long v = strtoll(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0 ||
                v < -2147483648LL || v > 2147483647LL) {
                snprintf(err, 256, "i32 operand out of range '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_i32(bc, (int32_t)v);
            return 0;
        }
        case BCODE_ASM_OP_I64: {
            long long v = strtoll(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0) {
                snprintf(err, 256, "invalid i64 operand '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_i64(bc, (int64_t)v);
            return 0;
        }
        case BCODE_ASM_OP_U8: {
            unsigned long long v = strtoull(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0 || v > 0xFFULL) {
                snprintf(err, 256, "u8 operand out of range '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_u8(bc, (uint8_t)v);
            return 0;
        }
        case BCODE_ASM_OP_U16: {
            unsigned long long v = strtoull(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0 || v > 0xFFFFULL) {
                snprintf(err, 256, "u16 operand out of range '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_u16(bc, (uint16_t)v);
            return 0;
        }
        case BCODE_ASM_OP_U64: {
            unsigned long long v = strtoull(buf, &endp, 10);
            if (endp == buf || *endp != '\0' || errno != 0) {
                snprintf(err, 256, "invalid u64 operand '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_u64(bc, (uint64_t)v);
            return 0;
        }
        case BCODE_ASM_OP_F32: {
            double d = strtod(buf, &endp);
            if (endp == buf || *endp != '\0') {
                snprintf(err, 256, "invalid f32 operand '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_f32(bc, (float)d);
            return 0;
        }
        case BCODE_ASM_OP_F64: {
            double d = strtod(buf, &endp);
            if (endp == buf || *endp != '\0') {
                snprintf(err, 256, "invalid f64 operand '%.*s'", (int)tlen, tk);
                return -1;
            }
            bcode_write_f64(bc, d);
            return 0;
        }
        case BCODE_ASM_OP_BOOL: {
            if (strcmp(buf, "0") == 0)      bcode_write_bool(bc, false);
            else if (strcmp(buf, "1") == 0) bcode_write_bool(bc, true);
            else {
                snprintf(err, 256, "bool operand must be 0 or 1, got '%.*s'",
                         (int)tlen, tk);
                return -1;
            }
            return 0;
        }
        default:
            snprintf(err, 256, "unsupported operand kind");
            return -1;
    }
}

/* ---- code 段一行：MNEMONIC [operands...] ---- */

static int parse_code_line(asm_parser_t *ps, allocator_t *alloc, bytecode_t *bc,
                           vec_t *fixups) {
    const char *tp = ps->p;
    while (tp < ps->end && isspace((unsigned char)*tp)) tp++;
    if (tp >= ps->end) return 0; /* 空行 */

    const char *mn = tp;
    while (tp < ps->end && !isspace((unsigned char)*tp)) tp++;
    size_t mlen = (size_t)(tp - mn);

    /* 未知 opcode 兜底：.byte <u32>（大小写无关） */
    if (mlen == 5 && strnieq(mn, ".byte", 5)) {
        while (tp < ps->end && isspace((unsigned char)*tp)) tp++;
        const char *tk = tp;
        while (tp < ps->end && !isspace((unsigned char)*tp)) tp++;
        size_t tlen = (size_t)(tp - tk);
        char buf[40];
        if (tlen == 0 || tlen >= sizeof buf) {
            snprintf(ps->err, sizeof ps->err, "malformed .byte operand");
            return -1;
        }
        memcpy(buf, tk, tlen); buf[tlen] = '\0';
        char *endp = NULL;
        unsigned long long v = strtoull(buf, &endp, 10);
        if (endp == buf || *endp != '\0' || v > 0xFFFFFFFFULL) {
            snprintf(ps->err, sizeof ps->err, "invalid .byte operand");
            return -1;
        }
        bcode_write_u32(bc, (uint32_t)v);
        return 0;
    }

    bcode_op_t op;
    const bcode_asm_entry_t *entry;
    if (!lookup_mnemonic(mn, mlen, &op, &entry)) {
        snprintf(ps->err, sizeof ps->err, "unknown mnemonic '%.*s'", (int)mlen, mn);
        return -1;
    }
    bcode_write_op(bc, op);

    size_t operand_idx = 0;
    for (;;) {
        while (tp < ps->end && isspace((unsigned char)*tp)) tp++;
        if (tp >= ps->end) break;

        if (operand_idx >= 4 || entry->operands[operand_idx] == BCODE_ASM_OP_NONE) {
            snprintf(ps->err, sizeof ps->err, "too many operands for '%.*s'",
                     (int)mlen, mn);
            return -1;
        }

        bcode_asm_operand_t kind = entry->operands[operand_idx];
        if (kind == BCODE_ASM_OP_STR) {
            /* 内联字符串字面量 "..."：解码 → intern 进 strtable → 写 u32 索引 */
            size_t slen = 0;
            const char *q = tp;
            if (decode_cstr(&q, ps->end, NULL, &slen) != 0) {
                snprintf(ps->err, sizeof ps->err, "invalid string literal operand");
                return -1;
            }
            uint8_t *sbuf = (uint8_t *)allocator_new_ex(
                alloc, "clux.vm.asm.str", slen ? slen : 1, NULL, NULL, NULL, 1);
            const char *q2 = tp;
            decode_cstr(&q2, ps->end, sbuf, &slen);
            size_t idx = bcode_str_index(
                bc, strslice_from_bytes((const char *)sbuf, slen));
            bcode_write_u32(bc, (uint32_t)idx);
            allocator_free(alloc, (void **)&sbuf);
            tp = q; /* 推进过字面量 */
        } else {
            const char *tk = tp;
            while (tp < ps->end && !isspace((unsigned char)*tp)) tp++;
            size_t tlen = (size_t)(tp - tk);

            /* [label] 引用：中括号包裹的标签名，与裸数字地址区分 */
            if (kind == BCODE_ASM_OP_U32 && tlen >= 2 &&
                tk[0] == '[' && tk[tlen - 1] == ']') {
                size_t nlen = tlen - 2;
                for (size_t i = 0; i < nlen; i++) {
                    if (!label_char_ok(tk[1 + i])) {
                        snprintf(ps->err, sizeof ps->err,
                                 "invalid label reference '%.*s'", (int)tlen, tk);
                        return -1;
                    }
                }
                asm_fixup_t *fx = (asm_fixup_t *)allocator_new_ex(
                    alloc, "clux.vm.asm.fixup", sizeof *fx, NULL, NULL, NULL, 1);
                fx->pos  = bc->code.len;   /* 占位 u32 之前的偏移 */
                fx->line = ps->line;
                memcpy(fx->name, tk + 1, nlen);
                fx->name[nlen] = '\0';
                vec_push(fixups, alloc, fx);
                bcode_write_u32(bc, 0);    /* 占位，待全部解析后回填 */
            } else if (parse_operand(bc, kind, tk, tlen, ps->err) != 0) {
                return -1;
            }
        }
        operand_idx++;
    }

    if (entry->operands[operand_idx] != BCODE_ASM_OP_NONE) {
        snprintf(ps->err, sizeof ps->err, "missing operands for '%.*s'",
                 (int)mlen, mn);
        return -1;
    }
    return 0;
}

/* ---- 顶层：逐行解析 ---- */

static int parse_text(allocator_t *alloc, const char *text, size_t len,
                      bytecode_t **out_bc) {
    bytecode_t *bc = bcode_new(alloc);
    if (!bc) {
        fprintf(stderr, "asm: out of memory\n");
        *out_bc = NULL;
        return 1;
    }

    vec_t *labels  = vec_new(alloc, /*owns_element=*/true);
    vec_t *fixups  = vec_new(alloc, /*owns_element=*/true);
    char errbuf[256];
    errbuf[0] = '\0';

    const char *cur = text;
    const char *end = text + len;
    size_t line_no = 0;

    while (cur < end) {
        line_no++;
        const char *ls = cur;
        while (cur < end && *cur != '\n') {
            if (*cur == '\r') { cur++; continue; }
            cur++;
        }
        const char *le = cur;
        if (cur < end && *cur == '\n') cur++;

        /* 去首尾空白 */
        while (ls < le && isspace((unsigned char)*ls)) ls++;
        while (le > ls && isspace((unsigned char)le[-1])) le--;

        asm_parser_t ps;
        ps.p = ls;
        ps.end = le;
        ps.line = line_no;
        ps.err[0] = '\0';

        if (ls >= le) continue; /* 空行 */

        /* 前导单行注释：去空白后以 ';' 起始的行整行跳过 */
        if (*ls == ';') continue;

        /* 节标记（如遗留的 [.section ...]）直接跳过，文本体仅指令行 */
        if (*ls == '[') continue;

        /* 标签定义行：去空白后形如 "name:"（无内嵌空格） */
        if (le > ls + 1 && le[-1] == ':' && memchr(ls, ' ', (size_t)(le - ls)) == NULL) {
            size_t nlen = (size_t)(le - 1 - ls);
            if (nlen == 0 || nlen >= sizeof(((asm_label_t *)0)->name)) {
                snprintf(errbuf, sizeof errbuf, "invalid label definition");
                goto fail;
            }
            for (size_t i = 0; i < nlen; i++) {
                if (!label_char_ok(ls[i])) {
                    snprintf(errbuf, sizeof errbuf, "invalid label name '%.*s'",
                             (int)nlen, ls);
                    goto fail;
                }
            }
            asm_label_t *lab = (asm_label_t *)allocator_new_ex(
                alloc, "clux.vm.asm.label", sizeof *lab, NULL, NULL, NULL, 1);
            memcpy(lab->name, ls, nlen);
            lab->name[nlen] = '\0';
            lab->pc = (uint32_t)bc->code.len;
            vec_push(labels, alloc, lab);
            continue;
        }

        int rc = parse_code_line(&ps, alloc, bc, fixups);

        if (rc != 0) {
            fprintf(stderr, "asm:%zu: error: %s\n", line_no, ps.err);
            bcode_destroy(&bc);
            *out_bc = NULL;
            vec_free(alloc, &labels);
            vec_free(alloc, &fixups);
            return 1;
        }
    }

    /* 回填 fixup：解析完成后所有标签 pc 已知，支持前向引用 */
    for (size_t i = 0; i < vec_len(fixups); i++) {
        asm_fixup_t *fx = (asm_fixup_t *)vec_get(fixups, i);
        uint32_t target = 0;
        bool found = false;
        for (size_t j = 0; j < vec_len(labels); j++) {
            asm_label_t *lab = (asm_label_t *)vec_get(labels, j);
            if (strcmp(lab->name, fx->name) == 0) {
                target = lab->pc;
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "asm:%zu: error: undefined label '%s'\n",
                    fx->line, fx->name);
            bcode_destroy(&bc);
            *out_bc = NULL;
            vec_free(alloc, &labels);
            vec_free(alloc, &fixups);
            return 1;
        }
        bcode_patch_u32(bc, fx->pos, target);
    }

    vec_free(alloc, &labels);
    vec_free(alloc, &fixups);
    *out_bc = bc;
    return 0;

fail:
    fprintf(stderr, "asm:%zu: error: %s\n", line_no, errbuf);
    bcode_destroy(&bc);
    *out_bc = NULL;
    vec_free(alloc, &labels);
    vec_free(alloc, &fixups);
    return 1;
}

int bcode_asm_parse(allocator_t *alloc, const char *text, size_t len,
                    bytecode_t **out_bc) {
    if (!alloc || !out_bc) return -1;
    *out_bc = NULL;
    if (!text) {
        fprintf(stderr, "asm: empty input\n");
        return 1;
    }
    return parse_text(alloc, text, len, out_bc);
}

int bcode_asm_from_file(allocator_t *alloc, const char *path, bytecode_t **out_bc) {
    if (!alloc || !path || !out_bc) return -1;
    *out_bc = NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "asm: cannot open file '%s'\n", path);
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 1; }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return 1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 1; }

    char *buf = (char *)allocator_new_ex(
        alloc, "clux.vm.asm.src", (size_t)size > 0 ? (size_t)size : 1,
        NULL, NULL, NULL, 1);
    size_t read = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) {
        fprintf(stderr, "asm: short read on '%s'\n", path);
        allocator_free(alloc, (void **)&buf);
        return 1;
    }

    int rc = parse_text(alloc, buf, (size_t)size, out_bc);
    allocator_free(alloc, (void **)&buf);
    return rc;
}
