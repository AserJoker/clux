#ifndef _H_CLUX_PARSER_AST_ASSIGN_
#define _H_CLUX_PARSER_AST_ASSIGN_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/lexer.h"

typedef struct {
    ast_node_t     base;
    strslice_t     name;        /* 赋值目标标识符 */
    const token_t *op;          /* 赋值运算符 token（= / += / -= / *= / /= / %=） */
    ast_node_t    *value;       /* 右值 */
} ast_assign_t;

static inline ast_node_t *ast_assign_new(arena_t *arena,
                                         uint32_t tok_begin, uint32_t tok_end) {
    ast_assign_t *n = (ast_assign_t *)arena_calloc(
        arena, 1, sizeof(ast_assign_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_ASSIGN;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
