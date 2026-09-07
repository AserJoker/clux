#include "parser/parse_stmt.h"
#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_ident.h"
#include "parser/ast_assign.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_discard.h"
#include "parser/ast_var_def.h"
#include "parser/ast_block.h"
#include "parser/ast_if.h"
#include "parser/ast_while.h"
#include "parser/ast_for.h"
#include "parser/ast_return.h"
#include "parser/ast_error.h"
#include "core/strslice.h"

/* ================================================================ */
/* parse_stmt: 语句分派器                                             */
/* ================================================================ */

ast_node_t *parse_stmt(parser_t *p) {
    if (check_keyword(p, "var"))    return parse_var_def(p);
    if (check_keyword(p, "if"))     return parse_if(p);
    if (check_keyword(p, "while"))  return parse_while(p);
    if (check_keyword(p, "for"))    return parse_for(p);
    if (check_keyword(p, "return")) return parse_return(p);
    if (check_keyword(p, "break"))  return parse_break(p);
    if (check_keyword(p, "continue")) return parse_continue(p);
    /* 最后尝试表达式语句（赋值已是表达式的一种） */
    return parse_assign_or_expr_stmt(p);
}

/* ================================================================ */
/* parse_break / parse_continue: break; / continue;                  */
/* ================================================================ */

ast_node_t *parse_break(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_keyword(p, "break")) return NULL;
    advance(p);
    skip_trivia(p);

    if (!expect_symbol(p, ";")) {
        return ast_error_new(p->arena, tb, p->pos,
                             "expected ';' after 'break'");
    }

    return ast_node_new(p->arena, AST_BREAK, tb, p->pos);
}

ast_node_t *parse_continue(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_keyword(p, "continue")) return NULL;
    advance(p);
    skip_trivia(p);

    if (!expect_symbol(p, ";")) {
        return ast_error_new(p->arena, tb, p->pos,
                             "expected ';' after 'continue'");
    }

    return ast_node_new(p->arena, AST_CONTINUE, tb, p->pos);
}

/* ================================================================ */
/* parse_assign_or_expr_stmt: 表达式 + ; → 语句                      */
/*                                                                  */
/* 赋值已在 parse_expr 中作为表达式处理（最低优先级、右结合）。         */
/* 此函数只负责消费末尾 ; 并将表达式包装为适当的语句节点。              */
/* ================================================================ */

ast_node_t *parse_assign_or_expr_stmt(parser_t *p) {
    uint32_t tb = p->pos;

    ast_node_t *expr = parse_expr(p);
    if (!expr) return NULL;
    if (expr->kind == AST_ERROR) return expr;

    skip_trivia(p);
    if (!expect_symbol(p, ";")) {
        if (expr->kind == AST_ASSIGN) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected ';' after assignment");
        }
        if (expr->kind == AST_DISCARD) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected ';' after discard statement");
        }
        return ast_error_new(p->arena, tb, p->pos,
                             "expected ';' after expression statement");
    }

    /* 赋值 / discard 已经是完整语句节点 */
    if (expr->kind == AST_ASSIGN || expr->kind == AST_DISCARD) return expr;

    /* 普通表达式 → 包装为 AST_EXPR_STMT */
    ast_node_t *stmt = ast_expr_stmt_new(p->arena, tb, p->pos);
    ((ast_expr_stmt_t *)stmt)->expr = expr;
    return stmt;
}
