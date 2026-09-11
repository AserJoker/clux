#ifndef _H_CLUX_PARSER_AST_TYPE_NAME_
#define _H_CLUX_PARSER_AST_TYPE_NAME_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/type_qual.h"

/**
 * 类型表达式节点：命名类型引用（M1 唯一形式）
 *
 * 类型即表达式（m2-design 关键架构决策 6）：一切类型槽位（var 标注 /
 * 函数参数与返回 / cast 目标 / .<type>{} 构造位）统一持 ast_node_t*。
 * M1 阶段类型表达式只有命名类型一种形式，const/volatile 前缀并入
 * qual 位（前缀式、右结合：const volatile i32 → volatile(const(i32))）。
 * M2 在此基础上扩展 AST_ARRAY_TYPE / AST_TUPLE_TYPE / AST_FUNC_TYPE 等。
 */
typedef struct {
    ast_node_t  base;
    strslice_t  name;  /* 类型名（如 "i32"、"Point"） */
    type_qual_t qual;  /* const/volatile 限定位（无修饰 = TYPE_QUAL_NONE） */
} ast_type_name_t;

static inline ast_node_t *ast_type_name_new(arena_t *arena,
                                            uint32_t tok_begin,
                                            uint32_t tok_end) {
    ast_type_name_t *n = (ast_type_name_t *)arena_calloc(
        arena, 1, sizeof(ast_type_name_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_TYPE_NAME;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

#ifdef __cplusplus
}
#endif
#endif
