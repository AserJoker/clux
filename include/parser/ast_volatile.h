#ifndef _H_CLUX_PARSER_AST_VOLATILE_
#define _H_CLUX_PARSER_AST_VOLATILE_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"

/* volatile 类型修饰：volatile <type-expr>（类型即表达式）。
 * 与 AST_CONST 同构：嵌套节点表达递归修饰，无顺序约束。 */
typedef struct {
    ast_node_t   base;
    ast_node_t  *sub;      /* 被修饰的类型表达式 */
} ast_volatile_t;

static inline ast_node_t *ast_volatile_new(arena_t *arena,
                                           uint32_t tok_begin, uint32_t tok_end) {
    ast_volatile_t *n = (ast_volatile_t *)arena_calloc(
        arena, 1, sizeof(ast_volatile_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_VOLATILE;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
