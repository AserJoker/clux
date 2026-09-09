#include "vm/type.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/function.h"
#include "core/panic.h"
#include "core/string.h"
#include "core/vec.h"
#include "vm/type_int.h"
#include "vm/type_float.h"
#include "vm/type_bool.h"
#include "vm/type_str.h"
#include "vm/type_void.h"
#include "vm/type_type.h"
#include "vm/type_func.h"
#include "vm/type_error.h"
#include "vm/type_interrupt.h"

#include <string.h>
#include <stdalign.h>

/* ---- type_find ---- */

const type_t *type_find(const vm_t *vm, strslice_t name) {
    if (!vm) return NULL;

    static const struct { const char *name; size_t offset; } entries[] = {
        { "i8",    offsetof(vm_t, type_i8)   },
        { "i16",   offsetof(vm_t, type_i16)  },
        { "i32",   offsetof(vm_t, type_i32)  },
        { "i64",   offsetof(vm_t, type_i64)  },
        { "u8",    offsetof(vm_t, type_u8)   },
        { "u16",   offsetof(vm_t, type_u16)  },
        { "u32",   offsetof(vm_t, type_u32)  },
        { "u64",   offsetof(vm_t, type_u64)  },
        { "f32",   offsetof(vm_t, type_f32)  },
        { "f64",   offsetof(vm_t, type_f64)  },
        { "bool",  offsetof(vm_t, type_bool) },
        { "str",   offsetof(vm_t, type_str)  },
        { "void",  offsetof(vm_t, type_void) },
        { "type",  offsetof(vm_t, type_type) },
        { "func",  offsetof(vm_t, type_func) },
        { "error", offsetof(vm_t, type_error) },
    };

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        if (strlen(entries[i].name) == name.len &&
            memcmp(entries[i].name, name.ptr, name.len) == 0) {
            type_t *const *slot = (type_t *const *)((const char *)vm + entries[i].offset);
            return *slot;
        }
    }
    return NULL;
}

/* ---- type_as_value ---- */

value_t *type_as_value(vm_t *vm, const type_t *t) {
    void *data = value_alloc_data_copy(vm->alloc, vm->type_type, &t);
    return value_make(vm, vm->type_type, data);
}

/* ---- type_promote ---- */

typedef enum {
    CAT_NONE    = 0,  /* 非数值（str/void/type/func/error） */
    CAT_BOOL    = 1,
    CAT_SINT    = 2,  /* 有符号整数 */
    CAT_UINT    = 3,  /* 无符号整数 */
    CAT_FLOAT   = 4,
} type_cat_t;

static type_cat_t type_category(const vm_t *vm, const type_t *t) {
    if (t == vm->type_bool) return CAT_BOOL;
    if (t == vm->type_i8 || t == vm->type_i16 ||
        t == vm->type_i32 || t == vm->type_i64) return CAT_SINT;
    if (t == vm->type_u8 || t == vm->type_u16 ||
        t == vm->type_u32 || t == vm->type_u64) return CAT_UINT;
    if (t == vm->type_f32 || t == vm->type_f64) return CAT_FLOAT;
    return CAT_NONE;
}

static int type_rank_val(const vm_t *vm, const type_t *t) {
    if (t == vm->type_bool) return 0;
    if (t == vm->type_i8)   return 1;
    if (t == vm->type_u8)   return 2;
    if (t == vm->type_i16)  return 3;
    if (t == vm->type_u16)  return 4;
    if (t == vm->type_i32)  return 5;
    if (t == vm->type_u32)  return 6;
    if (t == vm->type_i64)  return 7;
    if (t == vm->type_u64)  return 8;
    if (t == vm->type_f32)  return 9;
    if (t == vm->type_f64)  return 10;
    return -1;  /* 非数值类型 */
}

const type_t *type_promote(const vm_t *vm, const type_t *a, const type_t *b) {
    if (a == b) return a;

    type_cat_t ca = type_category(vm, a);
    type_cat_t cb = type_category(vm, b);

    if (ca == CAT_NONE || cb == CAT_NONE) return NULL;  /* 非数值类型 */
    if (ca != cb) return NULL;  /* 不同类别（bool/int/float）不协商 */

    int ra = type_rank_val(vm, a);
    int rb = type_rank_val(vm, b);
    return (ra >= rb) ? a : b;
}

/* ================================================================ */
/* 函数签名类型池（type_func_sig）                                       */
/* ================================================================ */

static bool func_sig_eq(const func_sig_t *a, const func_sig_t *b) {
    if (!a || !b) return false;
    if (a->param_count != b->param_count) return false;
    if (a->is_variadic != b->is_variadic) return false;
    if (a->return_type != b->return_type) return false;
    if (a->param_count > 0 &&
        memcmp(a->params, b->params, a->param_count * sizeof(type_t *)) != 0)
        return false;
    return true;
}

/* 构造规范类型名 "func(i32, i32): i32" / "func(...): void"，写入堆分配字符串 */
static char *func_sig_name(allocator_t *alloc, const type_t *const *params,
                           size_t param_count, const type_t *return_type,
                           bool is_variadic) {
    string_t *s = string_new(alloc);
    if (!s) return NULL;
    string_append_cstr(s, "func(");
    if (is_variadic && param_count == 0) {
        string_append_cstr(s, "...");
    } else {
        for (size_t i = 0; i < param_count; i++) {
            if (i > 0) string_append_cstr(s, ", ");
            if (params && params[i] && params[i]->name.ptr)
                string_append_bytes(s, params[i]->name.ptr, params[i]->name.len);
            else
                string_append_cstr(s, "?");
        }
        if (is_variadic) string_append_cstr(s, ", ...");
    }
    string_append_cstr(s, "): ");
    if (return_type && return_type->name.ptr)
        string_append_bytes(s, return_type->name.ptr, return_type->name.len);
    else
        string_append_cstr(s, "void");
    char *buf = allocator_new_ex(alloc, "char", sizeof(char), NULL, NULL, NULL,
                                 string_len(s) + 1);
    if (buf) {
        memcpy(buf, string_data(s), string_len(s));
        buf[string_len(s)] = '\0';
    }
    string_free(&s);
    return buf;
}

const type_t *type_func_sig(vm_t *vm, const type_t *const *params,
                            size_t param_count, const type_t *return_type,
                            bool is_variadic) {
    if (!vm || !vm->sig_types) return NULL;

    /* 去重扫描 */
    size_t n = vec_len(vm->sig_types);
    for (size_t i = 0; i < n; i++) {
        const func_type_t *ft = (const func_type_t *)vec_get(vm->sig_types, i);
        if (ft && func_sig_eq(&ft->sig, &(func_sig_t){
                .params = (const type_t **)params, .param_count = param_count,
                .return_type = return_type, .is_variadic = is_variadic }))
            return &ft->base;
    }

    /* 新建：func_type_t（base + 内联签名）+ params 数组 + name */
    func_type_t *ft = (func_type_t *)allocator_new_ex(
        vm->alloc, "func_type_t", sizeof(func_type_t), NULL, NULL, NULL, 1);
    if (!ft) panic("vm: out of memory allocating function signature type");
    memset(ft, 0, sizeof(func_type_t));

    if (param_count > 0) {
        const type_t **pcopy = (const type_t **)allocator_new_ex(
            vm->alloc, "type_t*", sizeof(type_t *), NULL, NULL, NULL,
            param_count);
        if (!pcopy) panic("vm: out of memory allocating function signature");
        memcpy(pcopy, params, param_count * sizeof(type_t *));
        ft->sig.params = pcopy;
    }
    ft->sig.param_count = param_count;
    ft->sig.return_type = return_type;
    ft->sig.is_variadic = is_variadic;

    char *name = func_sig_name(vm->alloc, params, param_count, return_type,
                               is_variadic);
    if (!name) panic("vm: out of memory allocating function signature name");

    ft->base.vtable = &VTABLE_FUNC;
    ft->base.name = (strslice_t){ name, strlen(name) };
    ft->base.size = sizeof(func_t *);
    ft->base.align = alignof(func_t *);

    vec_push(vm->sig_types, vm->alloc, ft);
    return &ft->base;
}

/* ================================================================ */
/* 内置类型实例（全局静态，由 vm_init_builtins 初始化）                   */
/* ================================================================ */

static type_t g_type_i8   = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_i16  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_i32  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_i64  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_u8   = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_u16  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_u32  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_u64  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_f32  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_f64  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_bool = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_str  = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_void = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_type = { NULL, {NULL,0}, 0, 0 };
/* func 基类声明为 func_type_t 布局：无签名（sig 全零），向下转型安全 */
static func_type_t g_type_func = { { NULL, {NULL,0}, 0, 0 }, { NULL, 0, NULL, false } };
static type_t g_type_error = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_interrupt = { NULL, {NULL,0}, 0, 0 };

void vm_init_builtins(vm_t *vm) {
    static const char S_I8[]  = "i8",   S_I16[] = "i16", S_I32[] = "i32", S_I64[] = "i64";
    static const char S_U8[]  = "u8",   S_U16[] = "u16", S_U32[] = "u32", S_U64[] = "u64";
    static const char S_F32[] = "f32",  S_F64[] = "f64";
    static const char S_BOOL[] = "bool", S_STR[] = "str";
    static const char S_VOID[] = "void", S_TYPE[] = "type", S_FUNC[] = "func";
    static const char S_ERROR[] = "error";
    static const char S_INTERRUPT[] = "interrupt";

    /* 函数签名类型池（type_func_sig intern 用）；元素由 vm_destroy 手动释放，
       vec 只持有指针数组（与 scope owned 同一模式） */
    vm->sig_types = vec_new(vm->alloc, /*owns_element=*/false);

    g_type_i8   = (type_t){ &VTABLE_INT_SIGNED,  STRSLICE_LIT(S_I8),  sizeof(int8_t),   alignof(int8_t) };
    g_type_i16  = (type_t){ &VTABLE_INT_SIGNED,  STRSLICE_LIT(S_I16), sizeof(int16_t),  alignof(int16_t) };
    g_type_i32  = (type_t){ &VTABLE_INT_SIGNED,  STRSLICE_LIT(S_I32), sizeof(int32_t),  alignof(int32_t) };
    g_type_i64  = (type_t){ &VTABLE_INT_SIGNED,  STRSLICE_LIT(S_I64), sizeof(int64_t),  alignof(int64_t) };
    g_type_u8   = (type_t){ &VTABLE_INT_UNSIGNED, STRSLICE_LIT(S_U8),  sizeof(uint8_t),   alignof(uint8_t) };
    g_type_u16  = (type_t){ &VTABLE_INT_UNSIGNED, STRSLICE_LIT(S_U16), sizeof(uint16_t),  alignof(uint16_t) };
    g_type_u32  = (type_t){ &VTABLE_INT_UNSIGNED, STRSLICE_LIT(S_U32), sizeof(uint32_t),  alignof(uint32_t) };
    g_type_u64  = (type_t){ &VTABLE_INT_UNSIGNED, STRSLICE_LIT(S_U64), sizeof(uint64_t),  alignof(uint64_t) };
    g_type_f32  = (type_t){ &VTABLE_FLOAT, STRSLICE_LIT(S_F32), sizeof(float),    alignof(float) };
    g_type_f64  = (type_t){ &VTABLE_FLOAT, STRSLICE_LIT(S_F64), sizeof(double),   alignof(double) };
    g_type_bool = (type_t){ &VTABLE_BOOL, STRSLICE_LIT(S_BOOL), sizeof(bool),     alignof(bool) };
    g_type_str  = (type_t){ &VTABLE_STR,  STRSLICE_LIT(S_STR),  sizeof(string_t*), alignof(string_t*) };
    g_type_void = (type_t){ &VTABLE_VOID, STRSLICE_LIT(S_VOID), 0, 1 };
    g_type_type = (type_t){ &VTABLE_TYPE, STRSLICE_LIT(S_TYPE), sizeof(const type_t*), alignof(const type_t*) };
    g_type_func.base = (type_t){ &VTABLE_FUNC, STRSLICE_LIT(S_FUNC), sizeof(func_t*), alignof(func_t*) };

    /* error_data_t 内联在 value data 块中 */
    g_type_error = (type_t){ &VTABLE_ERROR, STRSLICE_LIT(S_ERROR),
                             sizeof(error_data_t), alignof(error_data_t) };

    /* interrupt_data_t 内联在 value data 块中（引擎级控制流哨兵） */
    g_type_interrupt = (type_t){ &VTABLE_INTERRUPT, STRSLICE_LIT(S_INTERRUPT),
                                 sizeof(interrupt_data_t), alignof(interrupt_data_t) };

    vm->type_i8   = &g_type_i8;
    vm->type_i16  = &g_type_i16;
    vm->type_i32  = &g_type_i32;
    vm->type_i64  = &g_type_i64;
    vm->type_u8   = &g_type_u8;
    vm->type_u16  = &g_type_u16;
    vm->type_u32  = &g_type_u32;
    vm->type_u64  = &g_type_u64;
    vm->type_f32  = &g_type_f32;
    vm->type_f64  = &g_type_f64;
    vm->type_bool = &g_type_bool;
    vm->type_str  = &g_type_str;
    vm->type_void = &g_type_void;
    vm->type_type = &g_type_type;
    vm->type_func = &g_type_func.base;
    vm->type_error = &g_type_error;
    vm->type_interrupt = &g_type_interrupt;
}
