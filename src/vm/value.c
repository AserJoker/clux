#include "vm/value.h"
#include "vm/vm.h"
#include "vm/type_error.h"
#include "core/panic.h"
#include "core/string.h"

#include <string.h>

/* ---- value_t 结构体定义（仅此文件可见） ---- */

struct value_t {
    const type_t *type;
    void        *data;
};

/* ---- 内部分配 class_t（value_t 堆分配用） ---- */

static class_t g_value_class = {
    .name       = "clux.vm.value",
    .size       = sizeof(value_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

/** 内部 class_t（value data 堆分配用） */
static class_t g_value_data_class = {
    .name       = "clux.vm.value_data",
    .size       = 1,  /* 实际大小由 allocator_new 的 count 参数控制 */
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

value_t *value_alloc(allocator_t *alloc) {
    value_t *v = (value_t *)allocator_new(alloc, &g_value_class, 1);
    if (!v) panic("vm: out of memory allocating value");
    memset(v, 0, sizeof(value_t));
    return v;
}

void *value_alloc_data(allocator_t *alloc, const type_t *type) {
    if (!type || type->size == 0) return NULL;
    void *data = allocator_new(alloc, &g_value_data_class, type->size);
    if (!data) panic("vm: out of memory allocating value data");
    memset(data, 0, type->size);
    return data;
}

void *value_alloc_data_copy(allocator_t *alloc, const type_t *type, const void *src) {
    if (!type || type->size == 0) return NULL;
    void *data = allocator_new(alloc, &g_value_data_class, type->size);
    if (!data) panic("vm: out of memory allocating value data");
    memcpy(data, src, type->size);
    return data;
}

/* ---- 访问器 ---- */

const type_t *value_type(const value_t *v) {
    return v ? v->type : NULL;
}

void *value_data(const value_t *v) {
    return v ? v->data : NULL;
}

bool value_is_void(const value_t *v) {
    return !v || v->type == NULL;
}

/* ---- 构造器 ---- */

value_t *value_make_untracked(allocator_t *alloc, const type_t *type, void *data) {
    value_t *v = value_alloc(alloc);
    v->type = type;
    v->data = data;
    return v;
}

value_t *value_make(vm_t *vm, const type_t *type, void *data) {
    value_t *v = value_make_untracked(vm->alloc, type, data);
    scope_track(vm, vm->current_scope, v);
    return v;
}

/* ---- error 工具 ---- */

bool value_is_error(vm_t *vm, const value_t *v) {
    return v && vm && v->type == vm->type_error;
}

value_t *value_make_error(vm_t *vm, const char *message) {
    return value_make_error_loc(vm, message, NULL);
}

value_t *value_make_error_loc(vm_t *vm, const char *message, const char *location) {
    error_data_t ed;
    ed.message  = message  ? string_from_cstr(vm->alloc, message)  : NULL;
    ed.location = location ? string_from_cstr(vm->alloc, location) : NULL;
    void *data = value_alloc_data_copy(vm->alloc, vm->type_error, &ed);
    return value_make(vm, vm->type_error, data);
}

/* ---- 运算分派 ---- */

/*
 * value_xxx 薄封装：error 短路 + NULL vtable 检查 → 分派到 vtable
 * 类型协商、safe_cast 由各 vtable 函数通过 VTABLE_BINARY 自行处理
 */
#define DISPATCH(vm, a, b, op_name, op_sym)                                    \
    do {                                                                       \
        if (value_is_error((vm), (a))) return (a);                            \
        if (value_is_error((vm), (b))) return (b);                            \
        if (!(a)->type || !(a)->type->vtable || !(a)->type->vtable->op_name)  \
            return value_make_error(vm,                                        \
                "type does not support operator " op_sym);                     \
        return (a)->type->vtable->op_name(vm, a, b);                           \
    } while (0)

#define DISPATCH_UNARY(vm, a, op_name, op_sym)                                 \
    do {                                                                       \
        if (value_is_error((vm), (a))) return (a);                             \
        if (!(a)->type || !(a)->type->vtable || !(a)->type->vtable->op_name) { \
            return value_make_error(vm,                                        \
                "type does not support operator " op_sym);                     \
        }                                                                      \
        return (a)->type->vtable->op_name(vm, a);                              \
    } while (0)

value_t *value_add(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, add,  "+"); }
value_t *value_sub(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, sub,  "-"); }
value_t *value_mul(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, mul,  "*"); }
value_t *value_div(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, div,  "/"); }
value_t *value_mod(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, mod,  "%"); }
value_t *value_neg(vm_t *vm, value_t *a)              { DISPATCH_UNARY(vm, a, neg, "-"); }

value_t *value_eq(vm_t *vm, value_t *a, value_t *b)   { DISPATCH(vm, a, b, eq,   "=="); }
value_t *value_ne(vm_t *vm, value_t *a, value_t *b)   { DISPATCH(vm, a, b, ne,   "!="); }
value_t *value_lt(vm_t *vm, value_t *a, value_t *b)   { DISPATCH(vm, a, b, lt,   "<"); }
value_t *value_le(vm_t *vm, value_t *a, value_t *b)   { DISPATCH(vm, a, b, le,   "<="); }
value_t *value_gt(vm_t *vm, value_t *a, value_t *b)   { DISPATCH(vm, a, b, gt,   ">"); }
value_t *value_ge(vm_t *vm, value_t *a, value_t *b)   { DISPATCH(vm, a, b, ge,   ">="); }

value_t *value_band(vm_t *vm, value_t *a, value_t *b) { DISPATCH(vm, a, b, band, "&"); }
value_t *value_bor(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, bor,  "|"); }
value_t *value_bxor(vm_t *vm, value_t *a, value_t *b) { DISPATCH(vm, a, b, bxor, "^"); }
value_t *value_bnot(vm_t *vm, value_t *a)             { DISPATCH_UNARY(vm, a, bnot, "~"); }
value_t *value_shl(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, shl,  "<<"); }
value_t *value_shr(vm_t *vm, value_t *a, value_t *b)  { DISPATCH(vm, a, b, shr,  ">>"); }

value_t *value_lnot(vm_t *vm, value_t *a)             { DISPATCH_UNARY(vm, a, lnot, "!"); }

value_t *value_call(vm_t *vm, value_t *callee, value_t **args, size_t argc) {
    if (value_is_error(vm, callee)) return callee;
    /* 检查参数中是否有 error */
    for (size_t i = 0; i < argc; i++) {
        if (value_is_error(vm, args[i])) return args[i];
    }
    if (!callee->type || !callee->type->vtable || !callee->type->vtable->call) {
        return value_make_error(vm, "value is not callable");
    }
    return callee->type->vtable->call(vm, callee, args, argc);
}

/* ---- 生命周期 ---- */

void value_dispose(vm_t *vm, value_t *v) {
    if (!v) return;
    if (v->type) {
        if (v->type->vtable && v->type->vtable->dispose) {
            v->type->vtable->dispose(vm, v);
        }
        /* 释放 data 块 */
        if (v->data) {
            allocator_free(vm->alloc, (void **)&v->data);
        }
        v->type = NULL;
    }
}

value_t *value_clone(vm_t *vm, value_t *v) {
    if (!v || !v->type) return v;

    /* 通过 vtable clone 或默认 memcpy */
    value_t *r;
    if (v->type->vtable && v->type->vtable->clone) {
        r = v->type->vtable->clone(vm, v);
    } else {
        void *data = NULL;
        if (v->type->size > 0 && v->data) {
            data = value_alloc_data_copy(vm->alloc, v->type, v->data);
        }
        r = value_make(vm, v->type, data);
    }

    return r;
}

/* ---- 类型转换 ---- */

value_t *value_implicit_cast(vm_t *vm, value_t *v, const type_t *target) {
    if (value_is_error(vm, v)) return v;
    if (!v || !v->type) {
        return value_make_error(vm, "cannot implicitly cast void");
    }
    if (v->type == target) {
        /* 类型相同，clone 到当前 scope */
        return value_clone(vm, v);
    }
    if (v->type->vtable && v->type->vtable->implicit_cast) {
        return v->type->vtable->implicit_cast(vm, v, target);
    }
    return value_make_error(vm, "type does not support implicit cast");
}

value_t *value_explicit_cast(vm_t *vm, value_t *v, const type_t *target) {
    if (value_is_error(vm, v)) return v;
    if (!v || !v->type) {
        return value_make_error(vm, "cannot explicitly cast void");
    }
    if (v->type == target) {
        return value_clone(vm, v);
    }
    if (v->type->vtable && v->type->vtable->explicit_cast) {
        return v->type->vtable->explicit_cast(vm, v, target);
    }
    return value_make_error(vm, "type does not support explicit cast");
}
