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
 * value_t: 万物皆值（不透明类型）
 *
 * value_t 始终在堆上分配，由 scope 持有生命周期。
 * 所有 value_make / value_clone / value_implicit_cast / value_explicit_cast
 * 创建的 value 会自动 track 到 vm->current_scope。
 *
 * 外部代码通过 value_type() / value_data() 访问内部字段，
 * 不直接访问 struct value_t 成员。
 */
typedef struct value_t value_t;

/* ---- 访问器 ---- */

/** 获取 value 的类型 */
const type_t *value_type(const value_t *v);

/** 获取 value 的 data 指针 */
void *value_data(const value_t *v);

/** 便捷数据访问宏 */
#define value_as(v, T) (*((T *)(value_data(v))))

/** 判断 value 是否为 void（type==NULL 表示无类型） */
bool value_is_void(const value_t *v);

/** 判断 value 是否为 error（引擎级硬错误） */
bool value_is_error(vm_t *vm, const value_t *v);

/* ---- 构造器 ---- */

/** 从已有 data 指针构造 value（堆分配 + auto-track 到 current_scope）。
 * 接管 data 所有权。 */
value_t *value_make(vm_t *vm, const type_t *type, void *data);

/** 从已有 data 指针构造 value（堆分配，不 auto-track）。
 * 仅供需要手动管理生命周期时使用。 */
value_t *value_make_untracked(allocator_t *alloc, const type_t *type, void *data);

/* ---- error 构造 ---- */

/** 创建 error value（不带位置信息），message 为 C 字符串 */
value_t *value_make_error(vm_t *vm, const char *message);

/** 创建 error value（带位置信息） */
value_t *value_make_error_loc(vm_t *vm, const char *message, const char *location);

/* ---- 内存分配辅助 ---- */

/** 通过 allocator 分配一个 value_t（零初始化），用于 scope 存储 */
value_t *value_alloc(allocator_t *alloc);

/** 按 type->size/align 分配数据块，返回 data 指针 */
void *value_alloc_data(allocator_t *alloc, const type_t *type);

/** 分配数据块并用源数据初始化（memcpy） */
void *value_alloc_data_copy(allocator_t *alloc, const type_t *type, const void *src);

/* ---- 运算分派: a + b => a.type->vtable->add(vm, a, b) ---- */

value_t *value_add(vm_t *vm, value_t *a, value_t *b);
value_t *value_sub(vm_t *vm, value_t *a, value_t *b);
value_t *value_mul(vm_t *vm, value_t *a, value_t *b);
value_t *value_div(vm_t *vm, value_t *a, value_t *b);
value_t *value_mod(vm_t *vm, value_t *a, value_t *b);
value_t *value_neg(vm_t *vm, value_t *a);

value_t *value_eq(vm_t *vm, value_t *a, value_t *b);
value_t *value_ne(vm_t *vm, value_t *a, value_t *b);
value_t *value_lt(vm_t *vm, value_t *a, value_t *b);
value_t *value_le(vm_t *vm, value_t *a, value_t *b);
value_t *value_gt(vm_t *vm, value_t *a, value_t *b);
value_t *value_ge(vm_t *vm, value_t *a, value_t *b);

value_t *value_band(vm_t *vm, value_t *a, value_t *b);
value_t *value_bor(vm_t *vm, value_t *a, value_t *b);
value_t *value_bxor(vm_t *vm, value_t *a, value_t *b);
value_t *value_bnot(vm_t *vm, value_t *a);
value_t *value_shl(vm_t *vm, value_t *a, value_t *b);
value_t *value_shr(vm_t *vm, value_t *a, value_t *b);

value_t *value_lnot(vm_t *vm, value_t *a);

value_t *value_call(vm_t *vm, value_t *callee, value_t **args, size_t argc);

/* ---- 生命周期 ---- */

/** 销毁 value 的堆载荷（dispose data，不释放 value_t 结构体） */
void value_dispose(vm_t *vm, value_t *v);

/** 深拷贝 value 并自动注册到 vm->current_scope（scope 管理生命周期） */
value_t *value_clone(vm_t *vm, value_t *v);

/* ---- 类型转换 ---- */

value_t *value_implicit_cast(vm_t *vm, value_t *v, const type_t *target);
value_t *value_explicit_cast(vm_t *vm, value_t *v, const type_t *target);

/* ---- 显示 ---- */

void value_display(vm_t *vm, const value_t *v);

/* ---- vtable 实现辅助宏 ---- */

/*
 * VTABLE_BINARY: vtable 二元运算的标准前置流程
 *
 * 1. error 传播（短路）
 * 2. 同类型 → fall through，调用方做运算
 * 3. 不同类型：
 *    a. 两者均为数值类型 → promote → implicit_cast 两边 → re-dispatch
 *    b. 非数值类型 → 尝试右值 implicit_cast 到左值类型（右值兼容左值）
 *
 * 用法:
 *   static value_t *int_add(vm_t *vm, value_t *a, value_t *b) {
 *       VTABLE_BINARY(vm, a, b, add, "+");
 *       // 此时 value_type(a) == value_type(b)，做运算
 *       ...
 *   }
 */
#define VTABLE_BINARY(vm, a, b, slot, op_sym)                                \
    do {                                                                      \
        if (value_is_error((vm), (a))) return (a);                           \
        if (value_is_error((vm), (b))) return (b);                           \
        if (value_type((a)) != value_type((b))) {                            \
            const type_t *_rt = type_promote((vm), value_type((a)), value_type((b))); \
            if (_rt) {                                                        \
                value_t *_a2 = value_implicit_cast((vm), (a), _rt);          \
                if (value_is_error((vm), _a2)) return _a2;                   \
                value_t *_b2 = value_implicit_cast((vm), (b), _rt);          \
                if (value_is_error((vm), _b2)) return _b2;                   \
                return _rt->vtable->slot((vm), _a2, _b2);                    \
            }                                                                 \
            value_t *_b2 = value_implicit_cast((vm), (b), value_type((a)));  \
            if (value_is_error((vm), _b2)) return _b2;                       \
            return value_type((a))->vtable->slot((vm), (a), _b2);            \
        }                                                                     \
    } while (0)

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_VALUE_ */
