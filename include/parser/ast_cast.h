#ifndef _H_CLUX_PARSER_AST_CAST_
#define _H_CLUX_PARSER_AST_CAST_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
    strslice_t  target_type; /* 目标类型文本 */
} ast_cast_t;

static inline ast_node_t *ast_cast_new(arena_t *arena,
                                       uint32_t tok_begin, uint32_t tok_end) {
    ast_cast_t *n = (ast_cast_t *)arena_calloc(
        arena, 1, sizeof(ast_cast_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_CAST;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
