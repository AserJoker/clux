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

/** 左绑定力（中缀运算符左侧的绑定力） */
static int left_bp(int op) {
    switch (op) {
        /* 赋值 */   case '=':       return 2;
        /* 逻辑或 */ case '|':       return 4;
        /* 逻辑与 */ case '&':       return 6;
        /* 相等 */   case '=' + 256: return 10;  /* == */
                    case '!' + 256: return 10;  /* != */
        /* 比较 */   case '<':       return 12;
                    case '>':       return 12;
                    case '<' + 256: return 12;  /* <= */
                    case '>' + 256: return 12;  /* >= */
        /* 范围 */   case '.' + 256: return 14;  /* .. */
        /* 加减 */   case '+':       return 16;
                    case '-':       return 16;
        /* 乘除模 */ case '*':       return 18;
                    case '/':       return 18;
                    case '%':       return 18;
        /* 幂 */     case '^':       return 20;
        /* as */     case 'a' + 256: return 22;
        default:     return 0;
    }
}

/** 右绑定力（赋值右结合 → lbp+1；其余左结合 → lbp） */
static int right_bp(int op) {
    if (op == '=') return left_bp(op) + 1;
    return left_bp(op);
}

/**
 * 识别当前 token 的中缀运算符。
 * 返回 0 表示不是中缀运算符。
 * 使用 +256 编码区分复合运算符。
 */
static int infix_op(parser_t *p) {
    if (check_symbol(p, "==")) return '=' + 256;
    if (check_symbol(p, "!=")) return '!' + 256;
    if (check_symbol(p, "<=")) return '<' + 256;
    if (check_symbol(p, ">=")) return '>' + 256;
    if (check_symbol(p, "..")) return '.' + 256;
    if (check_keyword(p, "as")) return 'a' + 256;
    if (check_symbol(p, "="))  return '=';
    if (check_symbol(p, "+"))  return '+';
    if (check_symbol(p, "-"))  return '-';
    if (check_symbol(p, "*"))  return '*';
    if (check_symbol(p, "/"))  return '/';
    if (check_symbol(p, "%"))  return '%';
    if (check_symbol(p, "^"))  return '^';
    if (check_symbol(p, "<"))  return '<';
    if (check_symbol(p, ">"))  return '>';
    if (check_symbol(p, "&"))  return '&';
    if (check_symbol(p, "|"))  return '|';
    return 0;
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
            return ast_error_new(p->arena, tb, p->pos);
        }
        skip_trivia(p);
        if (!expect_symbol(p, ")")) {
            return ast_error_new(p->arena, tb, p->pos);
        }
        return inner;
    }

    return NULL;
}

/* ---- parse_unary: 前缀一元表达式 ---- */

ast_node_t *parse_unary(parser_t *p) {
    uint32_t tb = p->pos;

    int op = 0;
    if (check_symbol(p, "!"))      op = '!';
    else if (check_symbol(p, "~")) op = '~';
    else if (check_symbol(p, "-")) op = '-';

    if (op == 0) return parse_primary(p);

    advance(p);
    skip_trivia(p);

    ast_node_t *operand = parse_unary(p);
    if (!operand || operand->kind == AST_ERROR) {
        if (!operand) {
            parse_error(p, "expected expression after unary operator");
            return ast_error_new(p->arena, tb, p->pos);
        }
        return operand;
    }

    ast_node_t *node = ast_unary_new(p->arena, tb, p->pos);
    ((ast_unary_t *)node)->op      = op;
    ((ast_unary_t *)node)->operand = operand;
    return node;
}

/* ---- parse_postfix: 后缀表达式（贪婪循环） ---- */

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
                        parse_error(p, "expected expression in function call");
                        return ast_error_new(p->arena, tb, p->pos);
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
                            parse_error(p, "expected expression after ','");
                            return ast_error_new(p->arena, tb, p->pos);
                        }
                        return arg;
                    }
                    ast_append(&args, &args_last, NULL, arg);
                    skip_trivia(p);
                }
            }

            if (!expect_symbol(p, ")")) {
                return ast_error_new(p->arena, tb, p->pos);
            }

            ast_node_t *node = ast_call_new(p->arena, tb, p->pos);
            ((ast_call_t *)node)->callee    = lhs;
            ((ast_call_t *)node)->args      = args;
            ((ast_call_t *)node)->args_last = args_last;
            lhs = node;
            continue;
        }

        /* 成员访问：.field（注意排除 ".." 范围运算符） */
        if (check_symbol(p, ".")) {
            /* 判断是否是 ".."：下一个 token 也可能是 "." 组成 ".." */
            /* lexer 使用 maximal munch，所以 ".." 是一个 token，
               当前 token 是 "." 则不会是 ".." */
            uint32_t tb = p->pos;
            advance(p);  /* consume '.' */
            skip_trivia(p);

            if (!check_kind(p, TOKEN_TYPE_IDENTIFIER)) {
                parse_error(p, "expected field name after '.'");
                return ast_error_new(p->arena, tb, p->pos);
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
                        parse_error(p, "expected expression in index");
                        return ast_error_new(p->arena, tb, p->pos);
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
                            parse_error(p, "expected expression after ','");
                            return ast_error_new(p->arena, tb, p->pos);
                        }
                        return idx;
                    }
                    ast_append(&indices, &indices_last, NULL, idx);
                    skip_trivia(p);
                }
            }

            if (!expect_symbol(p, "]")) {
                return ast_error_new(p->arena, tb, p->pos);
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

/* ---- parse_expr_bp: Pratt 核心（内部，带最小绑定力） ---- */

static ast_node_t *parse_expr_bp(parser_t *p, int min_bp) {
    /* 1. 解析左侧：前缀 */
    ast_node_t *lhs = parse_unary(p);
    if (!lhs || lhs->kind == AST_ERROR) return lhs;

    /* 2. 后缀绑定力最高，贪婪消费 */
    lhs = parse_postfix(p, lhs);
    if (lhs->kind == AST_ERROR) return lhs;

    /* 3. 中缀循环 */
    for (;;) {
        skip_trivia(p);
        int op = infix_op(p);
        if (op == 0) break;

        int lbp = left_bp(op);
        if (lbp < min_bp) break;  /* 绑定力不够，让给上层 */

        uint32_t tb = p->pos;
        advance(p);
        skip_trivia(p);

        if (op == 'a' + 256) {
            /* as 类型转换：右侧是类型名 */
            if (!check_kind(p, TOKEN_TYPE_KEYWORD)) {
                parse_error(p, "expected type name after 'as'");
                return ast_error_new(p->arena, tb, p->pos);
            }
            strslice_t target_type = token_strslice(cur_token(p));
            advance(p);

            ast_node_t *node = ast_cast_new(p->arena, tb, p->pos);
            ((ast_cast_t *)node)->expr        = lhs;
            ((ast_cast_t *)node)->target_type = target_type;
            lhs = node;

            lhs = parse_postfix(p, lhs);
            if (lhs->kind == AST_ERROR) return lhs;
            continue;
        }

        /* 递归解析右侧，传入 right_bp 作为最小绑定力 */
        ast_node_t *rhs = parse_expr_bp(p, right_bp(op));
        if (!rhs || rhs->kind == AST_ERROR) {
            if (!rhs) {
                parse_error(p, "expected expression after operator");
                return ast_error_new(p->arena, tb, p->pos);
            }
            return rhs;
        }

        ast_node_t *node = ast_binary_new(p->arena, tb, p->pos);
        ((ast_binary_t *)node)->op  = op;
        ((ast_binary_t *)node)->lhs = lhs;
        ((ast_binary_t *)node)->rhs = rhs;
        lhs = node;

        lhs = parse_postfix(p, lhs);
        if (lhs->kind == AST_ERROR) return lhs;
    }

    return lhs;
}

/* ---- parse_expr: Pratt parser 公开入口 ---- */

ast_node_t *parse_expr(parser_t *p) {
    return parse_expr_bp(p, 0);
}
