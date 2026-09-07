#ifndef _H_CLUX_PARSER_AST_IDENT_
#define _H_CLUX_PARSER_AST_IDENT_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    strslice_t  name;        /* 标识符文本 */
} ast_ident_t;

static inline ast_node_t *ast_ident_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_ident_t *n = (ast_ident_t *)arena_calloc(
        arena, 1, sizeof(ast_ident_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_IDENT;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
