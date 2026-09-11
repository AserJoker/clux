#ifndef _H_CLUX_PARSER_AST_VAR_DEF_
#define _H_CLUX_PARSER_AST_VAR_DEF_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/parser.h"
#include <stdbool.h>

typedef struct {
    ast_node_t  base;
    strslice_t  name;        /* 变量名 */
    ast_node_t *type_expr;   /* 类型表达式（NULL = 推断；M1 只有 AST_TYPE_NAME） */
    ast_node_t *init;        /* 初始化表达式（必须存在；AST_UNDEF = 未初始化声明） */
    bool        is_comptime; /* comptime var：右值必须编译期可计算，引用点折叠为常量 */
} ast_var_def_t;

static inline ast_node_t *ast_var_def_new(arena_t *arena,
                                          uint32_t tok_begin, uint32_t tok_end) {
    ast_var_def_t *n = (ast_var_def_t *)arena_calloc(
        arena, 1, sizeof(ast_var_def_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_VAR_DEF;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/** 解析变量定义 var name[:type] = init; 初始化必须存在。 */
ast_node_t *parse_var_def(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
