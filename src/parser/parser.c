#include "parser/parser.h"
#include "core/panic.h"

/* ---- Internal: class for parser_t ---- */

static class_t g_parser_class = {
    .name = "clux.parser",
    .size = sizeof(char),
    .clone_fn = NULL,
    .move_fn = NULL,
    .dispose_fn = NULL,
};

/* ---- Public API ---- */

parser_t *parser_create(allocator_t *alloc, arena_t *arena, vec_t *tokens) {
    if (!alloc || !arena || !tokens) return NULL;

    parser_t *p = (parser_t *)allocator_new_ex(
        alloc,
        g_parser_class.name,
        /*size=*/1,
        /*move_fn=*/NULL,
        /*clone_fn=*/NULL,
        /*dispose_fn=*/NULL,
        /*count=*/sizeof(parser_t));
    if (!p) panic("parser: out of memory");

    p->alloc     = alloc;
    p->arena     = arena;
    p->tokens    = tokens;
    p->pos       = 0;
    return p;
}

void parser_destroy(parser_t **pp) {
    if (!pp || !*pp) return;
    parser_t *p = *pp;
    allocator_free(p->alloc, (void **)pp);
}
