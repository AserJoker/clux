#include "parser/ast_char_lit.h"
#include "parser/parse_utils.h"

/* ---- 内部辅助 ---- */

/**
 * 解析字符字面量的码点值。
 * 输入 text 含引号，如 'a' 或 '\n'。
 * clux 的 char 即 u8，值域 [0, 255]，多字节 UTF-8 码点应由 lexer 阶段拒绝。
 */
static uint32_t parse_char_value(strslice_t text) {
    /* 输入格式：'a' 或 '\n'，首尾是单引号 */
    if (text.len < 3 || text.ptr[0] != '\'' || text.ptr[text.len - 1] != '\'') return 0;
    const char *p = text.ptr;
    size_t inner_len = text.len - 2;  /* 去掉首尾引号后的内容长度 */

    if (inner_len == 0) return 0;

    if (p[1] == '\\') {
        /* escape 序列 */
        size_t consumed = 0;
        uint32_t v = parse_escape_seq(p + 2, inner_len - 1, &consumed);
        return v;
    }

    /* 普通字符：char 是 u8，只取单字节 */
    return (unsigned char)p[1];
}

/* ---- parse_char_lit ---- */

ast_node_t *parse_char_lit(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_kind(p, TOKEN_TYPE_CHARACTER)) { p->pos = tb; return NULL; }

    const token_t *t = cur_token(p);
    strslice_t text = token_strslice(t);
    advance(p);

    uint32_t value = parse_char_value(text);

    ast_node_t *node = ast_char_lit_new(p->arena, tb, p->pos);
    ((ast_char_lit_t *)node)->value = value;
    return node;
}
