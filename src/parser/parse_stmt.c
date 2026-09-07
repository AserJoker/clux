#include "parser/parse_stmt.h"
#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_ident.h"
#include "parser/ast_assign.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_discard.h"
#include "parser/ast_error.h"
#include "core/strslice.h"

/* ---- 辅助：检查当前 token 是否是赋值运算符 ---- */

static bool is_assign_op(parser_t *p) {
    return check_symbol(p, "=")  || check_symbol(p, "+=") ||
           check_symbol(p, "-=") || check_symbol(p, "*=") ||
           check_symbol(p, "/=") || check_symbol(p, "%=");
}

/* ---- 辅助：标识符文本是否为 "_" ---- */

static bool is_underscore(ast_node_t *node) {
    if (node->kind != AST_IDENT) return false;
    strslice_t name = ((ast_ident_t *)node)->name;
    return name.len == 1 && name.ptr[0] == '_';
}

/* ---- parse_assign_or_expr_stmt: 统一式 ---- */

ast_node_t *parse_assign_or_expr_stmt(parser_t *p) {
    uint32_t tb = p->pos;

    /* 1. 先解析一个表达式 */
    ast_node_t *expr = parse_expr(p);
    if (!expr) return NULL;
    if (expr->kind == AST_ERROR) return expr;

    /* 2. 分歧点：表达式后跟赋值运算符？ */
    if (is_assign_op(p)) {
        /* 只有 =（简单赋值）且左值是 _ → discard */
        if (check_symbol(p, "=") && is_underscore(expr)) {
            advance(p);
            skip_trivia(p);

            ast_node_t *value = parse_expr(p);
            if (!value || value->kind == AST_ERROR) {
                if (!value) {
                    return ast_error_new(p->arena, tb, p->pos,
                                         "expected expression after '_ ='");
                }
                return value;
            }

            skip_trivia(p);
            if (!expect_symbol(p, ";")) {
                return ast_error_new(p->arena, tb, p->pos,
                                     "expected ';' after discard statement");
            }

            ast_node_t *node = ast_discard_new(p->arena, tb, p->pos);
            ((ast_discard_t *)node)->expr = value;
            return node;
        }

        /* 赋值语句：左值必须是标识符 */
        if (expr->kind != AST_IDENT) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "invalid assignment target");
        }

        strslice_t name = ((ast_ident_t *)expr)->name;
        const token_t *op = cur_token(p);
        advance(p);
        skip_trivia(p);

        ast_node_t *value = parse_expr(p);
        if (!value || value->kind == AST_ERROR) {
            if (!value) {
                return ast_error_new(p->arena, tb, p->pos,
                                     "expected expression after assignment operator");
            }
            return value;
        }

        skip_trivia(p);
        if (!expect_symbol(p, ";")) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected ';' after assignment");
        }

        ast_node_t *node = ast_assign_new(p->arena, tb, p->pos);
        ((ast_assign_t *)node)->name  = name;
        ((ast_assign_t *)node)->op    = op;
        ((ast_assign_t *)node)->value = value;
        return node;
    }

    /* 3. 表达式语句：expr; */
    skip_trivia(p);
    if (!expect_symbol(p, ";")) {
        return ast_error_new(p->arena, tb, p->pos,
                             "expected ';' after expression statement");
    }

    ast_node_t *node = ast_expr_stmt_new(p->arena, tb, p->pos);
    ((ast_expr_stmt_t *)node)->expr = expr;
    return node;
}
