#ifndef _H_CLUX_PARSER_AST_UNDEF_
#define _H_CLUX_PARSER_AST_UNDEF_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"
#include "parser/parser.h"

/**
 * undefined 字面量节点（无额外字段）。
 * 用于 var name:type = undefined 的"未初始化声明"（TDZ 起点），
 * 初始化状态由 sema 数据流分析（flow_init）判定，运行时为 undefined 占位。
 */
static inline ast_node_t *ast_undef_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_node_t *n = (ast_node_t *)arena_calloc(
        arena, 1, sizeof(ast_node_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->kind      = AST_UNDEF;
    n->tok_begin = tok_begin;
    n->tok_end   = tok_end;
    return n;
}

/**
 * 解析 undefined 字面量（undefined 关键字）。
 * 不匹配返回 NULL。
 */
ast_node_t *parse_undef(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
