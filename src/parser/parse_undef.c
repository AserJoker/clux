#include "parser/ast_undef.h"
#include "parser/parse_utils.h"

ast_node_t *parse_undef(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_keyword(p, "undefined")) { p->pos = tb; return NULL; }

    const token_t *t = cur_token(p);
    (void)t;
    advance(p);

    return ast_undef_new(p->arena, tb, p->pos);
}
