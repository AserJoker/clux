#include "vm/bcode_serial.h"
#include "vm/bcode.h"
#include "core/allocator.h"
#include "core/string.h"
#include "core/strslice.h"
#include "core/vec.h"

#include <stdio.h>
#include <string.h>

/* ================================================================ */
/* 字节码二进制序列化 / 反序列化（.cxb）                              */
/* ================================================================ */
/* 格式（全部小端）：
 *   "CXBC" : 4 字节
 *   version : u32
 *   str_count : u32
 *   [str_len : u32][bytes : str_len]   × str_count
 *   code_len : u64
 *   code : code_len 字节
 */

/* ---- 小端编码辅助（与 bcode.c 的字节序约定一致） ---- */

static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_u64_le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

static uint32_t get_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= ((uint64_t)p[i] << (8 * i));
    return v;
}

/* ---- 增长式输出缓冲（alloc 管理） ---- */

typedef struct {
    allocator_t *alloc;
    uint8_t     *data;
    size_t       len;
    size_t       cap;
} serial_buf_t;

static void sb_append(serial_buf_t *sb, const void *src, size_t n) {
    if (sb->len + n > sb->cap) {
        size_t nc = sb->cap ? sb->cap : 256;
        while (nc < sb->len + n) nc *= 2;
        uint8_t *nd = (uint8_t *)allocator_new_ex(
            sb->alloc, "clux.vm.serial.buf", nc, NULL, NULL, NULL, 1);
        if (sb->data) {
            memcpy(nd, sb->data, sb->len);
            allocator_free(sb->alloc, (void **)&sb->data);
        }
        sb->data = nd;
        sb->cap  = nc;
    }
    memcpy(sb->data + sb->len, src, n);
    sb->len += n;
}

static void sb_u32(serial_buf_t *sb, uint32_t v) {
    uint8_t b[4];
    put_u32_le(b, v);
    sb_append(sb, b, sizeof b);
}

static void sb_u64(serial_buf_t *sb, uint64_t v) {
    uint8_t b[8];
    put_u64_le(b, v);
    sb_append(sb, b, sizeof b);
}

/* ================================================================ */
/* 序列化                                                             */
/* ================================================================ */

static uint8_t *serial_to_mem(allocator_t *alloc, const bytecode_t *bc,
                              size_t *out_len) {
    serial_buf_t sb = { alloc, NULL, 0, 0 };

    /* magic + version */
    sb_append(&sb, "CXBC", 4);
    sb_u32(&sb, BCODE_SERIAL_VERSION);

    /* strtable 段 */
    size_t scount = bcode_str_count(bc);
    sb_u32(&sb, (uint32_t)scount);
    for (size_t i = 0; i < scount; i++) {
        strslice_t s = bcode_str_at(bc, i);
        sb_u32(&sb, (uint32_t)s.len);
        if (s.len) sb_append(&sb, s.ptr, s.len);
    }

    /* code 段 */
    sb_u64(&sb, (uint64_t)bc->code.len);
    if (bc->code.len) sb_append(&sb, bc->code.data, bc->code.len);

    if (out_len) *out_len = sb.len;
    return sb.data;
}

uint8_t *bcode_serial_mem(allocator_t *alloc, const bytecode_t *bc,
                          size_t *out_len) {
    if (!alloc || !bc) return NULL;
    if (out_len) *out_len = 0;
    return serial_to_mem(alloc, bc, out_len);
}

int bcode_serial(const bytecode_t *bc, const char *out_path) {
    if (!bc || !out_path) return -1;

    size_t len = 0;
    uint8_t *buf = serial_to_mem(bc->alloc, bc, &len);
    if (!buf) {
        fprintf(stderr, "serial: out of memory\n");
        return -1;
    }

    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "serial: cannot open file '%s'\n", out_path);
        allocator_free(bc->alloc, (void **)&buf);
        return -1;
    }

    size_t wrote = fwrite(buf, 1, len, fp);
    int cf = fclose(fp);
    allocator_free(bc->alloc, (void **)&buf);

    if (wrote != len || cf != 0) {
        fprintf(stderr, "serial: short write on '%s'\n", out_path);
        return -1;
    }
    return 0;
}

/* ================================================================ */
/* 反序列化                                                           */
/* ================================================================ */

int bcode_deserial(allocator_t *alloc, const uint8_t *data, size_t len,
                   bytecode_t **out_bc) {
    if (!out_bc) return -1;
    *out_bc = NULL;
    if (!alloc || !data) {
        fprintf(stderr, "deserial: invalid input\n");
        return 1;
    }

    size_t off = 0;

    /* magic */
    if (len < 8 || memcmp(data, "CXBC", 4) != 0) {
        fprintf(stderr, "deserial: bad magic (not a .cxb file)\n");
        return 1;
    }
    off = 4;

    /* version */
    uint32_t version = get_u32_le(data + off);
    off += 4;
    if (version != BCODE_SERIAL_VERSION) {
        fprintf(stderr, "deserial: unsupported version %u (expected %u)\n",
                (unsigned)version, (unsigned)BCODE_SERIAL_VERSION);
        return 1;
    }

    /* strtable 段 */
    if (off + 4 > len) {
        fprintf(stderr, "deserial: truncated strtable header\n");
        return 1;
    }
    uint32_t scount = get_u32_le(data + off);
    off += 4;

    bytecode_t *bc = bcode_new(alloc);
    if (!bc) {
        fprintf(stderr, "deserial: out of memory\n");
        return 1;
    }

    for (uint32_t i = 0; i < scount; i++) {
        if (off + 4 > len) {
            fprintf(stderr, "deserial: truncated string length\n");
            bcode_destroy(&bc);
            return 1;
        }
        uint32_t slen = get_u32_le(data + off);
        off += 4;
        if (off + slen > len) {
            fprintf(stderr, "deserial: truncated string data\n");
            bcode_destroy(&bc);
            return 1;
        }
        /* 按序追加（不经过 intern 去重：保持索引与源模块严格一致） */
        string_t *copy = (slen == 0)
            ? string_new(bc->alloc)
            : string_from_bytes(bc->alloc, (const char *)(data + off), slen);
        vec_push(bc->strs, bc->alloc, copy);
        off += slen;
    }

    /* code 段 */
    if (off + 8 > len) {
        fprintf(stderr, "deserial: truncated code length\n");
        bcode_destroy(&bc);
        return 1;
    }
    uint64_t code_len = get_u64_le(data + off);
    off += 8;

    if (code_len > (uint64_t)(len - off)) {
        fprintf(stderr, "deserial: truncated code (declared %llu, avail %zu)\n",
                (unsigned long long)code_len, len - off);
        bcode_destroy(&bc);
        return 1;
    }
    if (code_len > 0) {
        /* 通过 writer API 逐字节写入，保持 code 缓冲由 bcode 拥有 */
        for (uint64_t i = 0; i < code_len; i++) {
            bcode_write_u8(bc, data[off + i]);
        }
    }

    *out_bc = bc;
    return 0;
}

int bcode_deserial_from_file(allocator_t *alloc, const char *path,
                             bytecode_t **out_bc) {
    if (!alloc || !path || !out_bc) return -1;
    *out_bc = NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "deserial: cannot open file '%s'\n", path);
        return 1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 1; }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return 1; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 1; }

    size_t n = (size_t)size;
    uint8_t *buf = (uint8_t *)allocator_new_ex(
        alloc, "clux.vm.serial.src", n > 0 ? n : 1, NULL, NULL, NULL, 1);
    size_t read = fread(buf, 1, n, fp);
    fclose(fp);

    if (read != n) {
        fprintf(stderr, "deserial: short read on '%s'\n", path);
        allocator_free(alloc, (void **)&buf);
        return 1;
    }

    int rc = bcode_deserial(alloc, buf, n, out_bc);
    allocator_free(alloc, (void **)&buf);
    return rc;
}
