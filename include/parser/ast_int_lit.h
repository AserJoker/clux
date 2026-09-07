#ifndef _H_CLUX_PARSER_AST_INT_LIT_
#define _H_CLUX_PARSER_AST_INT_LIT_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include "parser/parser.h"

typedef struct {
    ast_node_t  base;
    uint64_t    value;       /* 解析后的整数值 */
    strslice_t  type;        /* 类型后缀（"i32"/"u64" 等），可为空 */
} ast_int_lit_t;

static inline ast_node_t *ast_int_lit_new(arena_t *arena,
                                          uint32_t tok_begin, uint32_t tok_end) {
    ast_int_lit_t *n = (ast_int_lit_t *)arena_calloc(
        arena, 1, sizeof(ast_int_lit_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_INT_LIT;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;
    return &n->base;
}

/**
 * 解析整数字面量。
 * 不匹配返回 NULL，错误返回 AST_ERROR。
 */
ast_node_t *parse_int_lit(parser_t *p);

/**
 * 判断 TOKEN_TYPE_NUMERIC 的文本是否表示浮点数。
 */
bool numeric_is_float(const char *text, size_t len);

#ifdef __cplusplus
}
#endif
#endif
