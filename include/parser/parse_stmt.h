#ifndef _H_CLUX_PARSER_PARSE_STMT_
#define _H_CLUX_PARSER_PARSE_STMT_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/parser.h"
#include "parser/ast_node.h"

/**
 * 语句分派器。根据首 token 分派到对应 parse 函数。
 * 最后回退到 parse_assign_or_expr_stmt。
 */
ast_node_t *parse_stmt(parser_t *p);

/** break; */
ast_node_t *parse_break(parser_t *p);

/** continue; */
ast_node_t *parse_continue(parser_t *p);

/** 统一式：赋值 / 表达式语句 / discard 语句。 */
ast_node_t *parse_assign_or_expr_stmt(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
