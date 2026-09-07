#ifndef _H_CLUX_PARSER_AST_FOR_
#define _H_CLUX_PARSER_AST_FOR_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *init;        /* AST_VAR_DEF / AST_ASSIGN / NULL */
    ast_node_t *cond;        /* 表达式 / NULL */
    ast_node_t *update;      /* AST_ASSIGN / NULL */
    ast_node_t *body;        /* AST_BLOCK */
} ast_for_t;

static inline ast_node_t *ast_for_new(arena_t *arena,
                                      uint32_t tok_begin, uint32_t tok_end) {
    ast_for_t *n = (ast_for_t *)arena_calloc(
        arena, 1, sizeof(ast_for_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_FOR;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
