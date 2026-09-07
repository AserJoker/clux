#ifndef _H_CLUX_PARSER_AST_INDEX_
#define _H_CLUX_PARSER_AST_INDEX_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    ast_node_t *object;     /* [ 左边的表达式 */
    ast_node_t *indices;    /* 索引兄弟链（下标 / 泛型实参） */
    ast_node_t *indices_last;
} ast_index_t;

static inline ast_node_t *ast_index_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end) {
    ast_index_t *n = (ast_index_t *)arena_calloc(
        arena, 1, sizeof(ast_index_t), _Alignof(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_INDEX;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/**
 * 解析下标/泛型索引表达式（后缀 [...]）。
 * 由 parse_postfix 内部调用，不直接对外。
 */
ast_node_t *parse_index(parser_t *p);

#ifdef __cplusplus
}
#endif
#endif
