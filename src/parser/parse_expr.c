#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_ident.h"
#include "parser/ast_unary.h"
#include "parser/ast_error.h"

/* ---- parse_primary: 原子表达式入口 ---- */

ast_node_t *parse_primary(parser_t *p) {
    ast_node_t *node;

    /* bool 必须在 ident 前检查（true/false 是 keyword 不是 identifier） */
    node = parse_bool_lit(p);    if (node) return node;
    node = parse_int_lit(p);     if (node) return node;
    node = parse_float_lit(p);   if (node) return node;
    node = parse_string_lit(p);  if (node) return node;
    node = parse_char_lit(p);    if (node) return node;
    node = parse_ident(p);       if (node) return node;

    /* 分组表达式：(expr) */
    if (check_symbol(p, "(")) {
        uint32_t tb = p->pos;
        advance(p);  /* consume '(' */
        skip_trivia(p);
        ast_node_t *inner = parse_expr(p);
        if (!inner) {
            return ast_error_new(p->arena, tb, p->pos);
        }
        skip_trivia(p);
        if (!expect_symbol(p, ")")) {
            return ast_error_new(p->arena, tb, p->pos);
        }
        return inner;
    }

    /* 不匹配任何 primary */
    return NULL;
}

/* ---- parse_unary: 前缀一元表达式 ---- */

ast_node_t *parse_unary(parser_t *p) {
    uint32_t tb = p->pos;

    /* 检查前缀运算符：! ~ - */
    int op = 0;
    if (check_symbol(p, "!"))      op = '!';
    else if (check_symbol(p, "~")) op = '~';
    else if (check_symbol(p, "-")) op = '-';

    if (op == 0) {
        /* 不是一元前缀 → 降级到 primary */
        return parse_primary(p);
    }

    advance(p);  /* 消费前缀运算符 */
    skip_trivia(p);

    ast_node_t *operand = parse_unary(p);  /* 递归：支持 !!x 等 */
    if (!operand || operand->kind == AST_ERROR) {
        if (!operand) {
            parse_error(p, "expected expression after unary operator");
            return ast_error_new(p->arena, tb, p->pos);
        }
        return operand;  /* 传播 AST_ERROR */
    }

    ast_node_t *node = ast_unary_new(p->arena, tb, p->pos);
    ((ast_unary_t *)node)->op      = op;
    ((ast_unary_t *)node)->operand = operand;
    return node;
}

/* ---- parse_expr: Pratt parser 入口（M1 暂用简化版） ---- */

ast_node_t *parse_expr(parser_t *p) {
    /* M1 先用 parse_unary 作为入口，后续加入 Pratt 中缀循环 */
    return parse_unary(p);
}
