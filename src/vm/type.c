#include "vm/type.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/function.h"
#include "core/string.h"
#include "vm/type_int.h"
#include "vm/type_float.h"
#include "vm/type_bool.h"
#include "vm/type_str.h"
#include "vm/type_void.h"
#include "vm/type_type.h"
#include "vm/type_func.h"
#include "vm/type_error.h"

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
static type_t g_type_func = { NULL, {NULL,0}, 0, 0 };
static type_t g_type_error = { NULL, {NULL,0}, 0, 0 };

void vm_init_builtins(vm_t *vm) {
    static const char S_I8[]  = "i8",   S_I16[] = "i16", S_I32[] = "i32", S_I64[] = "i64";
    static const char S_U8[]  = "u8",   S_U16[] = "u16", S_U32[] = "u32", S_U64[] = "u64";
    static const char S_F32[] = "f32",  S_F64[] = "f64";
    static const char S_BOOL[] = "bool", S_STR[] = "str";
    static const char S_VOID[] = "void", S_TYPE[] = "type", S_FUNC[] = "func";
    static const char S_ERROR[] = "error";

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
    g_type_func = (type_t){ &VTABLE_FUNC, STRSLICE_LIT(S_FUNC), sizeof(func_t*), alignof(func_t*) };

    /* error_data_t 内联在 value data 块中 */
    g_type_error = (type_t){ &VTABLE_ERROR, STRSLICE_LIT(S_ERROR),
                             sizeof(error_data_t), alignof(error_data_t) };

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
    vm->type_func = &g_type_func;
    vm->type_error = &g_type_error;
}
