#include "parser/ast_bool_lit.h"
#include "parser/parse_utils.h"

ast_node_t *parse_bool_lit(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_keyword(p, "true") && !check_keyword(p, "false")) {
        p->pos = tb;
        return NULL;
    }

    bool value = check_keyword(p, "true");
    advance(p);

    ast_node_t *node = ast_bool_lit_new(p->arena, tb, p->pos);
    ((ast_bool_lit_t *)node)->value = value;
    return node;
}
