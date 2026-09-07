#ifndef _H_CLUX_PARSER_AST_ERROR_
#define _H_CLUX_PARSER_AST_ERROR_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    strslice_t  message;     /* 错误描述信息 */
    ast_node_t *node;        /* 发生错误的子节点（可为 NULL） */
} ast_error_t;

static inline ast_node_t *ast_error_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_error_t *n = (ast_error_t *)arena_calloc(
        arena, 1, sizeof(ast_error_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_ERROR;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
