#ifndef _H_CLUX_PARSER_AST_CHAR_LIT_
#define _H_CLUX_PARSER_AST_CHAR_LIT_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    uint32_t    value;       /* 解析后的 Unicode 码点 */
} ast_char_lit_t;

static inline ast_node_t *ast_char_lit_new(arena_t *arena,
                                           uint32_t tok_begin, uint32_t tok_end) {
    ast_char_lit_t *n = (ast_char_lit_t *)arena_calloc(
        arena, 1, sizeof(ast_char_lit_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_CHAR_LIT;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/**
 * 解析字符字面量。
 * 不匹配返回 NULL，错误返回 AST_ERROR。
 */
ast_node_t *parse_char_lit(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
