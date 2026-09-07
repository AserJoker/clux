#include "parser/ast_string_lit.h"
#include "parser/parse_utils.h"

#include <string.h>

/* ---- 内部辅助 ---- */

/**
 * 解析字符串字面量：去掉引号，展开 escape 序列，arena 分配结果。
 * 输入 text 含引号，如 "hello\n"。
 */
static strslice_t parse_string_value(arena_t *arena, strslice_t text) {
    /* 跳过前导 " */
    if (text.len < 2 || text.ptr[0] != '"') return STRSLICE_EMPTY;
    const char *p = text.ptr;
    size_t inner_len = text.len - 2;  /* 去掉首尾引号 */

    /* 两遍扫描：先计算展开后长度 */
    size_t out_len = 0;
    size_t i = 1;
    while (i < 1 + inner_len) {
        if (p[i] == '\\') {
            size_t consumed = 0;
            uint32_t cp = parse_escape_seq(p + i + 1, (1 + inner_len) - (i + 1), &consumed);
            out_len += utf8_encode_len(cp);
            i += 1 + consumed;
        } else {
            out_len++;
            i++;
        }
    }

    /* 分配并写入 */
    char *buf = (char *)arena_alloc(arena, out_len, _Alignof(char));
    if (!buf) return STRSLICE_EMPTY;

    size_t pos = 0;
    i = 1;
    while (i < 1 + inner_len) {
        if (p[i] == '\\') {
            size_t consumed = 0;
            uint32_t cp = parse_escape_seq(p + i + 1, (1 + inner_len) - (i + 1), &consumed);
            pos += utf8_encode(cp, buf + pos);
            i += 1 + consumed;
        } else {
            buf[pos++] = p[i++];
        }
    }

    return strslice_from_bytes(buf, out_len);
}

/* ---- parse_string_lit ---- */

ast_node_t *parse_string_lit(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_kind(p, TOKEN_TYPE_STRING)) { p->pos = tb; return NULL; }

    const token_t *t = cur_token(p);
    strslice_t text = token_strslice(t);
    advance(p);

    strslice_t resolved = parse_string_value(p->arena, text);

    ast_node_t *node = ast_string_lit_new(p->arena, tb, p->pos);
    ((ast_string_lit_t *)node)->text = resolved;
    return node;
}
