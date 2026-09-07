#include "parser/ast_float_lit.h"
#include "parser/ast_int_lit.h"  /* numeric_is_float */
#include "parser/ast_error.h"
#include "parser/parse_utils.h"

#include <stdlib.h>
#include <string.h>

/* ---- 内部辅助 ---- */

/** 浮点类型后缀白名单 */
typedef struct {
    const char *name;
    double      max_val;    /* 正无穷阈值：超出此值则溢出 */
} float_type_info_t;

static const float_type_info_t kFloatTypes[] = {
    {"f32", 3.402823466e+38},
    {"f64", 1.7976931348623157e+308},
};

static const float_type_info_t *find_float_type(strslice_t s) {
    for (size_t i = 0; i < sizeof(kFloatTypes) / sizeof(kFloatTypes[0]); i++) {
        if (strslice_eq(s, strslice_from_cstr(kFloatTypes[i].name)))
            return &kFloatTypes[i];
    }
    return NULL;
}

/**
 * 解析浮点字面量的值（数字 token 不含后缀）。
 */
static double parse_float_value(strslice_t text) {
    /* strtod 需要 NUL 终止，拷贝到栈缓冲 */
    char buf[64];
    size_t copy_len = text.len < sizeof(buf) - 1 ? text.len : sizeof(buf) - 1;
    memcpy(buf, text.ptr, copy_len);
    buf[copy_len] = '\0';

    return strtod(buf, NULL);
}

/* ---- parse_float_lit ---- */

ast_node_t *parse_float_lit(parser_t *p) {
    uint32_t tb = p->pos;

    if (!check_kind(p, TOKEN_TYPE_NUMERIC)) { p->pos = tb; return NULL; }
    const token_t *t = cur_token(p);
    strslice_t text = token_strslice(t);

    if (!numeric_is_float(text.ptr, text.len)) { p->pos = tb; return NULL; }

    advance(p);
    skip_trivia(p);

    double value = parse_float_value(text);

    /* 检查紧跟的类型后缀（类型名是 keyword token） */
    strslice_t type = STRSLICE_EMPTY;
    if (check_kind(p, TOKEN_TYPE_KEYWORD)) {
        strslice_t next = token_strslice(cur_token(p));
        const float_type_info_t *ti = find_float_type(next);
        if (ti) {
            /* 校验 value 是否超出 type 表示范围 */
            if (value > ti->max_val) {
                parse_error(p, "float literal out of range for type %.*s",
                            (int)next.len, next.ptr);
                advance(p);
                ast_node_t *err = ast_error_new(p->arena, tb, p->pos);
                return err;
            }
            type = next;
            advance(p);
        }
    }

    ast_node_t *node = ast_float_lit_new(p->arena, tb, p->pos);
    ((ast_float_lit_t *)node)->value = value;
    ((ast_float_lit_t *)node)->type  = type;
    return node;
}
