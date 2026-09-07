#ifndef _H_CLUX_PARSER_AST_WHILE_
#define _H_CLUX_PARSER_AST_WHILE_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *cond;
    ast_node_t *body;        /* AST_BLOCK */
} ast_while_t;

static inline ast_node_t *ast_while_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_while_t *n = (ast_while_t *)arena_calloc(
        arena, 1, sizeof(ast_while_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_WHILE;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/** 解析 while cond { body }。 */
ast_node_t *parse_while(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
