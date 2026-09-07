#ifndef _H_CLUX_PARSER_AST_UNARY_
#define _H_CLUX_PARSER_AST_UNARY_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"
#include "parser/lexer.h"

typedef struct {
    ast_node_t       base;
    const token_t   *op;         /* 前缀运算符 token（零拷贝引用 token pool） */
    ast_node_t      *operand;
} ast_unary_t;

static inline ast_node_t *ast_unary_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_unary_t *n = (ast_unary_t *)arena_calloc(
        arena, 1, sizeof(ast_unary_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_UNARY;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
