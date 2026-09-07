#ifndef _H_CLUX_PARSER_AST_EXPR_STMT_
#define _H_CLUX_PARSER_AST_EXPR_STMT_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
} ast_expr_stmt_t;

static inline ast_node_t *ast_expr_stmt_new(arena_t *arena,
                                            uint32_t tok_begin, uint32_t tok_end) {
    ast_expr_stmt_t *n = (ast_expr_stmt_t *)arena_calloc(
        arena, 1, sizeof(ast_expr_stmt_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_EXPR_STMT;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
