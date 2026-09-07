#ifndef _H_CLUX_PARSER_AST_IF_
#define _H_CLUX_PARSER_AST_IF_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *cond;
    ast_node_t *then_body;   /* AST_BLOCK */
    ast_node_t *else_body;   /* AST_BLOCK 或 NULL */
} ast_if_t;

static inline ast_node_t *ast_if_new(arena_t *arena,
                                     uint32_t tok_begin, uint32_t tok_end) {
    ast_if_t *n = (ast_if_t *)arena_calloc(
        arena, 1, sizeof(ast_if_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_IF;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
