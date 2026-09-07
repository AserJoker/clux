#ifndef _H_CLUX_PARSER_PARSE_STMT_
#define _H_CLUX_PARSER_PARSE_STMT_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/parser.h"
#include "parser/ast_node.h"

/**
 * 统一式：赋值 / 表达式语句 / discard 语句。
 *
 * 先解析表达式，再根据后接 token 分派：
 *   - 后接 = / += / -= / *= / /= / %= → AST_ASSIGN
 *   - 左值为 _ 且后接 = → AST_DISCARD
 *   - 后接 ; → AST_EXPR_STMT
 *
 * 不匹配返回 NULL，错误返回 AST_ERROR。
 */
ast_node_t *parse_assign_or_expr_stmt(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_PARSER_PARSE_STMT_ */
