#ifndef _H_CLUX_VM_TYPE_
#define _H_CLUX_VM_TYPE_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/vtable.h"
#include "core/strslice.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * func_sig_t: 函数签名（参数 + 返回值信息）
 *
 * 内联在 func_type_t 中。同一签名的所有函数共享同一个签名类型
 * func_type_t（由 vm 类型池按签名去重 intern），因此 func value 的
 * type 即携带完整签名，shadow 值（data=NULL）也能完成调用类型检查。
 */
typedef struct func_sig_t {
    const type_t **params;      /* array[param_count]; 元素可为 NULL = 无类型约束 */
    size_t         param_count;
    const type_t  *return_type; /* NULL = void */
    bool           is_variadic;
} func_sig_t;

/**
 * type_t: 类型描述符（基类）
 *
 * 每种类型对应一个 type_t 单例，由 vm 统一管理。
 * 类型本身也是一种 value（通过 type_as_value 获取）。
 *
 * 需要附加信息的类型通过 C 继承扩展：子结构体首成员为 type_t base，
 * 向上转型 (type_t *) 使用，向下转型 (func_type_t *) 读取扩展字段。
 */
typedef struct type_t {
    const vtable_t *vtable;
    strslice_t      name;       /* 类型名，如 "i32", "f64", "str" */
    size_t          size;       /* 该类型数据的字节大小 */
    size_t          align;      /* 该类型数据的对齐要求 */
} type_t;

/**
 * func_type_t: 函数签名类型（type_t 的扩展）
 *
 * 首成员 base 必须为 type_t（向上转型安全）。sig 携带参数/返回值信息，
 * 由 vm 类型池 intern（type_func_sig），vm 拥有生命周期。vm->type_func
 * 是无签名的 func 基类，也是 func_type_t 布局（sig 为空），向下转型安全。
 */
typedef struct func_type_t {
    type_t      base;
    func_sig_t  sig;
} func_type_t;

/** 根据名称在 vm 的类型注册表中查找类型，未找到返回 NULL */
const type_t *type_find(const vm_t *vm, strslice_t name);

/** 判断两个类型是否相同（指针比较，因为类型是单例） */
static inline bool type_eq(const type_t *a, const type_t *b) {
    return a == b;
}

/** 将 type 转为 value_t*（type 作为 first-class value） */
value_t *type_as_value(vm_t *vm, const type_t *t);

/**
 * 类型提升（二元运算前协商结果类型）
 *
 * 优先级：f64 > f32 > u64 > i64 > u32 > i32 > u16 > i16 > u8 > i8 > bool
 * - 同类型直接返回
 * - 两个数值类型返回高 rank 的那个
 * - 非数值类型（str/void/type/func/error）或类型不兼容返回 NULL
 */
const type_t *type_promote(const vm_t *vm, const type_t *a, const type_t *b);

/**
 * 函数签名类型 intern（按签名去重）
 *
 * 以 params（可为 NULL 或含 NULL 元素 = 无类型约束）、param_count、
 * return_type、is_variadic 标识一个函数签名。相同签名返回同一个
 * type_t（vm 类型池持有，vm_destroy 时释放）。params 数组会被复制，
 * 调用方的临时数组可自行释放。
 */
const type_t *type_func_sig(vm_t *vm, const type_t *const *params,
                            size_t param_count, const type_t *return_type,
                            bool is_variadic);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_ */
