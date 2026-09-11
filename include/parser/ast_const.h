#ifndef _H_CLUX_PARSER_AST_CONST_
#define _H_CLUX_PARSER_AST_CONST_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"

/* const 类型修饰：const <type-expr>（类型即表达式）。
 * const/volatile 是真实类型 kind，嵌套节点自然表达递归修饰，
 * 无谁先谁后约束（volatile const i32 / const const i32 均合法，
 * 消费层按嵌套顺序收敛）。 */
typedef struct {
    ast_node_t   base;
    ast_node_t  *sub;      /* 被修饰的类型表达式 */
} ast_const_t;

static inline ast_node_t *ast_const_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_const_t *n = (ast_const_t *)arena_calloc(
        arena, 1, sizeof(ast_const_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_CONST;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
