#ifndef _H_CLUX_VM_VALUE_INTERNAL_
#define _H_CLUX_VM_VALUE_INTERNAL_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/value.h"
#include "vm/type.h"

/**
 * value_internal.h: value_t 内部实现细节
 *
 * 仅限 .c 实现文件 include，不对外暴露 value_t 结构体定义。
 * 外部代码通过 value.h 中的 opaque typedef 和访问器函数使用 value_t。
 */

struct value_t {
    const type_t *type;
    void        *data;
};

/* 内部数据访问宏（仅实现文件使用） */
#define value_as(v, T) (*((T *)((v)->data)))

/**
 * 从已有 data 指针构造 value（堆分配 + auto-track 到 current_scope）。
 * 接管 data 所有权。
 */
value_t *value_make(vm_t *vm, const type_t *type, void *data);

/**
 * 从已有 data 指针构造 value（堆分配，不 auto-track）。
 * 仅供内部需要手动管理生命周期时使用（如 scope_track 的实现）。
 */
value_t *value_make_untracked(allocator_t *alloc, const type_t *type, void *data);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_VALUE_INTERNAL_ */
