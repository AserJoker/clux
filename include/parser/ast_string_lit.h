#ifndef _H_CLUX_PARSER_AST_STRING_LIT_
#define _H_CLUX_PARSER_AST_STRING_LIT_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    strslice_t  text;        /* escape 展开后的文本，arena 分配 */
} ast_string_lit_t;

static inline ast_node_t *ast_string_lit_new(arena_t *arena,
                                             uint32_t tok_begin, uint32_t tok_end) {
    ast_string_lit_t *n = (ast_string_lit_t *)arena_calloc(
        arena, 1, sizeof(ast_string_lit_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_STRING_LIT;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/**
 * 解析字符串字面量。
 * 不匹配返回 NULL，错误返回 AST_ERROR。
 */
ast_node_t *parse_string_lit(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
