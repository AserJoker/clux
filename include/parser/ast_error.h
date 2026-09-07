#ifndef _H_CLUX_PARSER_AST_ERROR_
#define _H_CLUX_PARSER_AST_ERROR_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include "parser/ast_node.h"
#include <stdarg.h>
#include <stdio.h>

typedef struct {
    ast_node_t  base;
    strslice_t  message;     /* 错误描述信息（arena 分配，NUL 终止） */
} ast_error_t;

/**
 * 创建错误节点（printf 风格格式化消息）。
 * 消息文本会被复制到 arena 上以保证生命周期。
 * 示例：ast_error_new(arena, tb, pos, "expected '%s' but got '%s'", ")", "]");
 */
static inline ast_node_t *ast_error_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end,
                                        const char *fmt, ...)
#ifdef __cplusplus
    __attribute__((format(printf, 4, 5)))
#elif defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
;

/**
 * va_list 版本，供内部使用。
 */
static inline ast_node_t *ast_error_newv(arena_t *arena,
                                         uint32_t tok_begin, uint32_t tok_end,
                                         const char *fmt, va_list ap)
#ifdef __cplusplus
    __attribute__((format(printf, 4, 0)))
#elif defined(__GNUC__)
    __attribute__((format(printf, 4, 0)))
#endif
;

/* ---- inline 实现 ---- */

static inline ast_node_t *ast_error_newv(arena_t *arena,
                                         uint32_t tok_begin, uint32_t tok_end,
                                         const char *fmt, va_list ap) {
    ast_error_t *n = (ast_error_t *)arena_calloc(
        arena, 1, sizeof(ast_error_t), ALIGNOF(max_align_t));
    if (!n) return NULL;
    n->base.kind      = AST_ERROR;
    n->base.tok_begin = tok_begin;
    n->base.tok_end   = tok_end;

    if (fmt) {
        /* 先计算所需长度 */
        va_list ap2;
        va_copy(ap2, ap);
        int len = vsnprintf(NULL, 0, fmt, ap2);
        va_end(ap2);

        if (len > 0) {
            char *buf = (char *)arena_alloc(arena, (size_t)len + 1, 1);
            if (buf) {
                vsnprintf(buf, (size_t)len + 1, fmt, ap);
                n->message.ptr = buf;
                n->message.len = (size_t)len;
            }
        }
    }

    return &n->base;
}

static inline ast_node_t *ast_error_new(arena_t *arena,
                                        uint32_t tok_begin, uint32_t tok_end,
                                        const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ast_node_t *result = ast_error_newv(arena, tok_begin, tok_end, fmt, ap);
    va_end(ap);
    return result;
}

#ifdef __cplusplus
}
#endif
#endif
