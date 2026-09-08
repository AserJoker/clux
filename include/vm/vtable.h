#ifndef _H_CLUX_VM_VTABLE_
#define _H_CLUX_VM_VTABLE_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/strslice.h"
#include <stdbool.h>
#include <stddef.h>

/* ---- Forward declarations ---- */

typedef struct vm_t    vm_t;
typedef struct type_t  type_t;
typedef struct value_t value_t;

/**
 * vtable_t: 类型行为虚表
 *
 * 所有运算通过虚表分派: a + b => a.type->vtable->add(vm, a, b)
 * 不支持的运算置 NULL，调用时触发运行时 panic。
 *
 * 所有 value 参数均为 value_t* 指针（堆分配，scope 持有生命周期）。
 */
typedef struct vtable_t {
    /* ---- 算术运算 ---- */
    value_t *(*add)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*sub)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*mul)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*div)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*mod)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*neg)(vm_t *vm, value_t *a);

    /* ---- 比较运算 ---- */
    value_t *(*eq)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*ne)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*lt)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*le)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*gt)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*ge)(vm_t *vm, value_t *a, value_t *b);

    /* ---- 位运算 ---- */
    value_t *(*band)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*bor)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*bxor)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*bnot)(vm_t *vm, value_t *a);
    value_t *(*shl)(vm_t *vm, value_t *a, value_t *b);
    value_t *(*shr)(vm_t *vm, value_t *a, value_t *b);

    /* ---- 逻辑运算 ---- */
    value_t *(*lnot)(vm_t *vm, value_t *a);

    /* ---- 调用（函数类型） ---- */
    value_t *(*call)(vm_t *vm, value_t *callee, value_t **args, size_t argc);

    /* ---- 生命周期 ---- */
    void    (*dispose)(vm_t *vm, value_t *v);
    value_t *(*clone)(vm_t *vm, value_t *v);

    /* ---- 类型转换 ---- */
    value_t *(*implicit_cast)(vm_t *vm, value_t *v, const type_t *target);
    value_t *(*explicit_cast)(vm_t *vm, value_t *v, const type_t *target);

    /* ---- 显示 ---- */
    void    (*display)(vm_t *vm, const value_t *v);
} vtable_t;

/** 全零 vtable（所有函数指针为 NULL） */
extern const vtable_t VTABLE_ZERO;

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_VTABLE_ */
