#include "parser/ast_var_def.h"
#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_error.h"

ast_node_t *parse_var_def(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_keyword(p, "var")) return NULL;
    advance(p);
    skip_trivia(p);

    /* 变量名：标识符 */
    if (!check_kind(p, TOKEN_TYPE_IDENTIFIER)) {
        return ast_error_new(p->arena, tb, p->pos,
                             "expected variable name after 'var'");
    }
    strslice_t name = token_strslice(cur_token(p));
    advance(p);
    skip_trivia(p);

    /* 可选类型标注：:type */
    strslice_t type_name = STRSLICE_EMPTY;
    type_qual_t type_qual = TYPE_QUAL_NONE;
    if (check_symbol(p, ":")) {
        advance(p);
        skip_trivia(p);

        if (!parse_type_spec(p, &type_name, &type_qual)) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected type name after ':'");
        }
    }

    /* 初始化表达式：= expr（必须） */
    if (!check_symbol(p, "=")) {
        return ast_error_new(p->arena, tb, p->pos,
                             "expected '=' and initializer in var definition");
    }
    advance(p);
    skip_trivia(p);

    ast_node_t *init = parse_expr(p);
    if (!init || init->kind == AST_ERROR) {
        if (!init) {
            return ast_error_new(p->arena, tb, p->pos,
                                 "expected expression after '=' in var definition");
        }
        return init;
    }
    skip_trivia(p);

    if (!expect_symbol(p, ";")) {
        return ast_error_new(p->arena, tb, p->pos,
                             "expected ';' after var definition");
    }

    ast_node_t *node = ast_var_def_new(p->arena, tb, p->pos);
    ((ast_var_def_t *)node)->name      = name;
    ((ast_var_def_t *)node)->type_name = type_name;
    ((ast_var_def_t *)node)->type_qual = type_qual;
    ((ast_var_def_t *)node)->init      = init;
    return node;
}
