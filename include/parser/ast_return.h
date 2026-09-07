#ifndef _H_CLUX_PARSER_AST_RETURN_
#define _H_CLUX_PARSER_AST_RETURN_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *value;       /* NULL = return; */
} ast_return_t;

static inline ast_node_t *ast_return_new(arena_t *arena,
                                         uint32_t tok_begin, uint32_t tok_end) {
    ast_return_t *n = (ast_return_t *)arena_calloc(
        arena, 1, sizeof(ast_return_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_RETURN;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/** 解析 return [expr]; */
ast_node_t *parse_return(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
