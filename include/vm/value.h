#ifndef _H_CLUX_VM_VALUE_
#define _H_CLUX_VM_VALUE_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/type.h"
#include "core/allocator.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * value_t: 万物皆值
 *
 * 16 bytes on 64-bit: type 指针 + data 指针。
 * data 始终指向按 type->size/align 独立分配的内存块。
 * 纯值语义：赋值即深拷贝，无引用/指针（M1）。
 */
typedef struct value_t {
    const type_t *type;
    void        *data;
} value_t;

/* ---- 便捷数据访问宏 ---- */

#define value_as(v, T) (*((T *)((v).data)))

/* ---- 构造器 ---- */

/** 从已有 data 指针构造 value（不拷贝 data，接管所有权） */
static inline value_t value_make(const type_t *type, void *data) {
    value_t val;
    val.type = type;
    val.data = data;
    return val;
}

/** 判断 value 是否为 void（type==NULL 表示无类型） */
static inline bool value_is_void(const value_t *v) {
    return v->type == NULL;
}

/** 判断 value 是否为 error（引擎级硬错误） */
bool value_is_error(vm_t *vm, const value_t *v);

/* ---- error 构造 ---- */

/** 创建 error value（不带位置信息），message 为 C 字符串 */
value_t value_make_error(vm_t *vm, const char *message);

/** 创建 error value（带位置信息） */
value_t value_make_error_loc(vm_t *vm, const char *message, const char *location);

/** 通过 allocator 分配一个 value_t（零初始化），用于 scope 存储 */
value_t *value_alloc(allocator_t *alloc);

/** 按 type->size/align 分配数据块，返回 data 指针 */
void *value_alloc_data(allocator_t *alloc, const type_t *type);

/** 分配数据块并用源数据初始化（memcpy） */
void *value_alloc_data_copy(allocator_t *alloc, const type_t *type, const void *src);

/* ---- 运算分派: a + b => a.type->vtable->add(vm, a, b) ---- */

value_t value_add(vm_t *vm, value_t a, value_t b);
value_t value_sub(vm_t *vm, value_t a, value_t b);
value_t value_mul(vm_t *vm, value_t a, value_t b);
value_t value_div(vm_t *vm, value_t a, value_t b);
value_t value_mod(vm_t *vm, value_t a, value_t b);
value_t value_neg(vm_t *vm, value_t a);

value_t value_eq(vm_t *vm, value_t a, value_t b);
value_t value_ne(vm_t *vm, value_t a, value_t b);
value_t value_lt(vm_t *vm, value_t a, value_t b);
value_t value_le(vm_t *vm, value_t a, value_t b);
value_t value_gt(vm_t *vm, value_t a, value_t b);
value_t value_ge(vm_t *vm, value_t a, value_t b);

value_t value_band(vm_t *vm, value_t a, value_t b);
value_t value_bor(vm_t *vm, value_t a, value_t b);
value_t value_bxor(vm_t *vm, value_t a, value_t b);
value_t value_bnot(vm_t *vm, value_t a);
value_t value_shl(vm_t *vm, value_t a, value_t b);
value_t value_shr(vm_t *vm, value_t a, value_t b);

value_t value_lnot(vm_t *vm, value_t a);

value_t value_call(vm_t *vm, value_t callee, value_t *args, size_t argc);

/* ---- 生命周期 ---- */

/** 销毁 value 的堆载荷（不释放 value_t 本身） */
void value_dispose(vm_t *vm, value_t *v);

/** 深拷贝 value 并自动注册到 vm->current_scope（scope 管理生命周期） */
value_t value_clone(vm_t *vm, value_t v);

/* ---- 类型转换 ---- */

value_t value_implicit_cast(vm_t *vm, value_t v, const type_t *target);
value_t value_explicit_cast(vm_t *vm, value_t v, const type_t *target);

/* ---- 显示 ---- */

void value_display(vm_t *vm, const value_t *v);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_VALUE_ */
