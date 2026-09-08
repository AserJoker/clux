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
 * type_t: 类型描述符
 *
 * 每种类型对应一个 type_t 单例，由 vm 统一管理。
 * 类型本身也是一种 value（通过 type_as_value 获取）。
 */
typedef struct type_t {
    const vtable_t *vtable;
    strslice_t      name;       /* 类型名，如 "i32", "f64", "str" */
    size_t          size;       /* 该类型数据的字节大小 */
    size_t          align;      /* 该类型数据的对齐要求 */
} type_t;

/** 根据名称在 vm 的类型注册表中查找类型，未找到返回 NULL */
const type_t *type_find(const vm_t *vm, strslice_t name);

/** 判断两个类型是否相同（指针比较，因为类型是单例） */
static inline bool type_eq(const type_t *a, const type_t *b) {
    return a == b;
}

/** 将 type 转为 value_t（type 作为 first-class value） */
value_t type_as_value(vm_t *vm, const type_t *t);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_ */
