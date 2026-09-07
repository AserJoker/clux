#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_ident.h"
#include "parser/ast_unary.h"
#include "parser/ast_binary.h"
#include "parser/ast_call.h"
#include "parser/ast_member.h"
#include "parser/ast_index.h"
#include "parser/ast_cast.h"
#include "parser/ast_error.h"

/* ---- Pratt parser 绑定力表 ---- */

/**
 * 查询中缀运算符的左右绑定力。
 * 遵循 M1 文档 4.1 节定义。
 *
 * 左结合运算符：right_prec = left_prec + 1
 * 返回 false 表示当前 token 不是中缀运算符。
 */
static bool infix_binding(const token_t *tok, int *lp, int *rp) {
    if (token_get_kind(tok) != TOKEN_TYPE_SYMBOL &&
        token_get_kind(tok) != TOKEN_TYPE_KEYWORD) {
        return false;
    }

    strslice_t s = token_strslice(tok);

    /* 关键字运算符 */
    if (s.len == 2 && s.ptr[0] == 'a' && s.ptr[1] == 's') {
        *lp = 21; *rp = 22;
        return true;
    }

    /* 双字符符号运算符 */
    if (s.len == 2) {
        if (s.ptr[0] == '|' && s.ptr[1] == '|') { *lp = 1;  *rp = 2;  return true; }
        if (s.ptr[0] == '&' && s.ptr[1] == '&') { *lp = 3;  *rp = 4;  return true; }
        if (s.ptr[0] == '=' && s.ptr[1] == '=') { *lp = 11; *rp = 12; return true; }
        if (s.ptr[0] == '!' && s.ptr[1] == '=') { *lp = 11; *rp = 12; return true; }
        if (s.ptr[0] == '<' && s.ptr[1] == '=') { *lp = 13; *rp = 14; return true; }
        if (s.ptr[0] == '>' && s.ptr[1] == '=') { *lp = 13; *rp = 14; return true; }
        if (s.ptr[0] == '<' && s.ptr[1] == '<') { *lp = 15; *rp = 16; return true; }
        if (s.ptr[0] == '>' && s.ptr[1] == '>') { *lp = 15; *rp = 16; return true; }
        return false;
    }

    /* 单字符符号运算符 */
    if (s.len == 1) {
        switch (s.ptr[0]) {
        case '|': *lp = 5;  *rp = 6;  return true;
        case '^': *lp = 7;  *rp = 8;  return true;
        case '&': *lp = 9;  *rp = 10; return true;
        case '<': *lp = 13; *rp = 14; return true;
        case '>': *lp = 13; *rp = 14; return true;
        case '+': *lp = 17; *rp = 18; return true;
        case '-': *lp = 17; *rp = 18; return true;
        case '*': *lp = 19; *rp = 20; return true;
        case '/': *lp = 19; *rp = 20; return true;
        case '%': *lp = 19; *rp = 20; return true;
        default:  return false;
        }
    }

    return false;
}

/* ---- parse_primary: 原子表达式入口 ---- */

ast_node_t *parse_primary(parser_t *p) {
    ast_node_t *node;

    node = parse_bool_lit(p);    if (node) return node;
    node = parse_float_lit(p);   if (node) return node;
    node = parse_int_lit(p);     if (node) return node;
    node = parse_string_lit(p);  if (node) return node;
    node = parse_char_lit(p);    if (node) return node;
    node = parse_ident(p);       if (node) return node;

    /* 分组表达式：(expr) */
    if (check_symbol(p, "(")) {
        uint32_t tb = p->pos;
        advance(p);
        skip_trivia(p);
        ast_node_t *inner = parse_expr(p);
        if (!inner) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected expression after '('");
        }
        if (inner->kind == AST_ERROR) return inner;
        skip_trivia(p);
        if (!expect_symbol(p, ")")) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected ')' after grouped expression");
        }
        return inner;
    }

    return NULL;
}

/* ---- parse_unary: 前缀一元表达式 ---- */

/** 前缀运算符右绑定力 = 23（M1 文档） */
#define PREFIX_RIGHT_PREC 23

ast_node_t *parse_unary(parser_t *p) {
    uint32_t tb = p->pos;

    const token_t *op_tok = NULL;
    if (check_symbol(p, "!"))      op_tok = cur_token(p);
    else if (check_symbol(p, "~")) op_tok = cur_token(p);
    else if (check_symbol(p, "-")) op_tok = cur_token(p);

    if (!op_tok) return parse_primary(p);

    advance(p);
    skip_trivia(p);

    ast_node_t *operand = parse_expr_prec(p, PREFIX_RIGHT_PREC);
    if (!operand || operand->kind == AST_ERROR) {
        if (!operand) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected expression after unary operator");
        }
        return operand;
    }

    ast_node_t *node = ast_unary_new(p->arena, tb, p->pos);
    ((ast_unary_t *)node)->op      = op_tok;
    ((ast_unary_t *)node)->operand = operand;
    return node;
}

/* ---- parse_postfix: 后缀表达式（绑定力 25，贪婪循环） ---- */

/** 后缀绑定力 = 25（M1 文档，函数调用最高） */
#define POSTFIX_LEFT_PREC 25

static ast_node_t *parse_postfix(parser_t *p, ast_node_t *lhs) {
    for (;;) {
        skip_trivia(p);

        /* 函数调用：(args...) */
        if (check_symbol(p, "(")) {
            uint32_t tb = p->pos;
            advance(p);
            skip_trivia(p);

            ast_node_t *args = NULL, *args_last = NULL;

            if (!check_symbol(p, ")")) {
                ast_node_t *arg = parse_expr(p);
                if (!arg || arg->kind == AST_ERROR) {
                    if (!arg) {
                        return ast_error_new(p->arena, tb, p->pos,
                                             "expected expression in function call");
                    }
                    return arg;
                }
                ast_append(&args, &args_last, NULL, arg);

                skip_trivia(p);
                while (check_symbol(p, ",")) {
                    advance(p);
                    skip_trivia(p);
                    arg = parse_expr(p);
                    if (!arg || arg->kind == AST_ERROR) {
                        if (!arg) {
                            return ast_error_new(p->arena, tb, p->pos,
                                                 "expected expression after ','");
                        }
                        return arg;
                    }
                    ast_append(&args, &args_last, NULL, arg);
                    skip_trivia(p);
                }
            }

            if (!expect_symbol(p, ")")) {
                return ast_error_new(p->arena, tb, p->pos,
                                     "expected ')' after function call arguments");
            }

            ast_node_t *node = ast_call_new(p->arena, tb, p->pos);
            ((ast_call_t *)node)->callee    = lhs;
            ((ast_call_t *)node)->args      = args;
            ((ast_call_t *)node)->args_last = args_last;
            lhs = node;
            continue;
        }

        /* 成员访问：.field */
        if (check_symbol(p, ".")) {
            uint32_t tb = p->pos;
            advance(p);
            skip_trivia(p);

            if (!check_kind(p, TOKEN_TYPE_IDENTIFIER)) {
                return ast_error_new(p->arena, tb, p->pos,
                                     "expected field name after '.'");
            }
            strslice_t field = token_strslice(cur_token(p));
            advance(p);

            ast_node_t *node = ast_member_new(p->arena, tb, p->pos);
            ((ast_member_t *)node)->object = lhs;
            ((ast_member_t *)node)->field  = field;
            lhs = node;
            continue;
        }

        /* 下标 / 泛型索引：[expr, ...] */
        if (check_symbol(p, "[")) {
            uint32_t tb = p->pos;
            advance(p);
            skip_trivia(p);

            ast_node_t *indices = NULL, *indices_last = NULL;

            if (!check_symbol(p, "]")) {
                ast_node_t *idx = parse_expr(p);
                if (!idx || idx->kind == AST_ERROR) {
                    if (!idx) {
                        return ast_error_new(p->arena, tb, p->pos,
                                             "expected expression in index");
                    }
                    return idx;
                }
                ast_append(&indices, &indices_last, NULL, idx);

                skip_trivia(p);
                while (check_symbol(p, ",")) {
                    advance(p);
                    skip_trivia(p);
                    idx = parse_expr(p);
                    if (!idx || idx->kind == AST_ERROR) {
                        if (!idx) {
                            return ast_error_new(p->arena, tb, p->pos,
                                                 "expected expression after ','");
                        }
                        return idx;
                    }
                    ast_append(&indices, &indices_last, NULL, idx);
                    skip_trivia(p);
                }
            }

            if (!expect_symbol(p, "]")) {
                return ast_error_new(p->arena, tb, p->pos,
                                     "expected ']' after index expression");
            }

            ast_node_t *node = ast_index_new(p->arena, tb, p->pos);
            ((ast_index_t *)node)->object       = lhs;
            ((ast_index_t *)node)->indices      = indices;
            ((ast_index_t *)node)->indices_last = indices_last;
            lhs = node;
            continue;
        }

        break;
    }
    return lhs;
}

/* ---- parse_expr_prec: Pratt 核心 ---- */

ast_node_t *parse_expr_prec(parser_t *p, int min_prec) {
    /* 1. 前缀：一元或原子 */
    ast_node_t *left = parse_unary(p);
    if (!left || left->kind == AST_ERROR) return left;

    /* 2. 中缀/后缀循环 */
    for (;;) {
        skip_trivia(p);

        /* 2a. 后缀绑定力最高(25)，贪婪消费 */
        if (check_symbol(p, "(") || check_symbol(p, ".") || check_symbol(p, "[")) {
            if (POSTFIX_LEFT_PREC < min_prec) break;
            left = parse_postfix(p, left);
            if (left->kind == AST_ERROR) return left;
            continue;
        }

        /* 2b. 中缀运算符查表 */
        const token_t *op_tok = cur_token(p);
        int lp, rp;
        if (!infix_binding(op_tok, &lp, &rp)) break;
        if (lp < min_prec) break;

        uint32_t op_pos = p->pos;
        advance(p);
        skip_trivia(p);

        /* as 特殊处理：右侧是类型名，不是表达式 */
        if (lp == 21) {
            if (!check_kind(p, TOKEN_TYPE_KEYWORD)) {
                return ast_error_new(p->arena, op_pos, p->pos,
                                     "expected type name after 'as'");
            }
            strslice_t target_type = token_strslice(cur_token(p));
            advance(p);

            ast_node_t *node = ast_cast_new(p->arena, op_pos, p->pos);
            ((ast_cast_t *)node)->expr        = left;
            ((ast_cast_t *)node)->target_type = target_type;
            left = node;
            continue;
        }

        /* 递归解析右侧，传入右绑定力作为最小绑定力 */
        ast_node_t *rhs = parse_expr_prec(p, rp);
        if (!rhs || rhs->kind == AST_ERROR) {
            if (!rhs) {
                return ast_error_new(p->arena, op_pos, p->pos,
                                     "expected expression after operator");
            }
            return rhs;
        }

        ast_node_t *node = ast_binary_new(p->arena, op_pos, p->pos);
        ((ast_binary_t *)node)->op  = op_tok;
        ((ast_binary_t *)node)->lhs = left;
        ((ast_binary_t *)node)->rhs = rhs;
        left = node;
    }

    return left;
}

/* ---- parse_expr: Pratt parser 公开入口 ---- */

ast_node_t *parse_expr(parser_t *p) {
    return parse_expr_prec(p, 0);
}
