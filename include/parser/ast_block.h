#ifndef _H_CLUX_PARSER_AST_BLOCK_
#define _H_CLUX_PARSER_AST_BLOCK_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *stmts;       /* 语句兄弟链首 */
    ast_node_t *stmts_last;  /* O(1) 追加 */
} ast_block_t;

static inline ast_node_t *ast_block_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_block_t *n = (ast_block_t *)arena_calloc(
        arena, 1, sizeof(ast_block_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_BLOCK;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
