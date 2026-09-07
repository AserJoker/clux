#include "parser/ast_ident.h"
#include "parser/parse_utils.h"

ast_node_t *parse_ident(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_kind(p, TOKEN_TYPE_IDENTIFIER)) { p->pos = tb; return NULL; }

    const token_t *t = cur_token(p);
    strslice_t name = token_strslice(t);
    advance(p);

    ast_node_t *node = ast_ident_new(p->arena, tb, p->pos);
    ((ast_ident_t *)node)->name = name;
    return node;
}
