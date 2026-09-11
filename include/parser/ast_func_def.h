#ifndef _H_CLUX_PARSER_AST_FUNC_DEF_
#define _H_CLUX_PARSER_AST_FUNC_DEF_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    strslice_t  name;        /* 函数名 */
    ast_node_t *params;      /* AST_VAR_DEF 兄弟链 */
    ast_node_t *params_last; /* O(1) 追加 */
    strslice_t  return_type; /* 返回类型文本（空切片 = void） */
    ast_node_t *body;        /* AST_BLOCK */
    bool        is_comptime; /* comptime func：调用点编译期求值折叠为常量，不注册到运行时 */
} ast_func_def_t;

static inline ast_node_t *ast_func_def_new(arena_t *arena,
                                           uint32_t tok_begin, uint32_t tok_end) {
    ast_func_def_t *n = (ast_func_def_t *)arena_calloc(
        arena, 1, sizeof(ast_func_def_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_FUNC_DEF;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/** 解析函数定义：func name(params):type { body } */
ast_node_t *parse_func_def(parser_t *p);

/**
 * func 统一入口（语句级 / 表达式级）。
 * expected_kind = AST_FUNC_DEF → 语句级函数定义（有 name）
 * M2+: expected_kind = AST_FUNC_LIT → 表达式级匿名函数
 */
ast_node_t *parse_func_like(parser_t *p, ast_kind_t expected_kind);

#ifdef __cplusplus
}
#endif
#endif
