#ifndef _H_CLUX_PARSER_PARSE_EXPR_
#define _H_CLUX_PARSER_PARSE_EXPR_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/parser.h"
#include "parser/ast_node.h"

/**
 * Pratt parser 入口。解析完整表达式。
 * 不匹配返回 NULL，错误返回 AST_ERROR。
 */
ast_node_t *parse_expr(parser_t *p);

/**
 * 解析原子表达式（字面量 / 标识符 / 分组）。
 * 不匹配返回 NULL，错误返回 AST_ERROR。
 */
ast_node_t *parse_primary(parser_t *p);

/**
 * 解析一元前缀表达式（! / ~ / -）。
 * 不匹配返回 NULL（非一元前缀），错误返回 AST_ERROR。
 */
ast_node_t *parse_unary(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_PARSER_PARSE_EXPR_ */
