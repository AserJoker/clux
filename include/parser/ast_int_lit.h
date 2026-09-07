#ifndef _H_CLUX_PARSER_AST_INT_LIT_
#define _H_CLUX_PARSER_AST_INT_LIT_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    strslice_t  text;        /* 原始切片（含后缀，如 "42u64"） */
} ast_int_lit_t;

static inline ast_node_t *ast_int_lit_new(arena_t *arena,
                                          uint32_t tok_begin, uint32_t tok_end) {
    ast_int_lit_t *n = (ast_int_lit_t *)arena_calloc(
        arena, 1, sizeof(ast_int_lit_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_INT_LIT;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
