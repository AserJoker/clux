#ifndef _H_CLUX_PARSER_AST_CALL_
#define _H_CLUX_PARSER_AST_CALL_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    strslice_t  name;        /* 函数名 */
    ast_node_t *args;        /* 实参兄弟链 */
    ast_node_t *args_last;   /* O(1) 追加 */
} ast_call_t;

static inline ast_node_t *ast_call_new(arena_t *arena,
                                       uint32_t tok_begin, uint32_t tok_end) {
    ast_call_t *n = (ast_call_t *)arena_calloc(
        arena, 1, sizeof(ast_call_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_CALL;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
