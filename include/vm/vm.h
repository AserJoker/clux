#ifndef _H_CLUX_VM_VM_
#define _H_CLUX_VM_VM_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/type.h"
#include "vm/value.h"
#include "vm/scope.h"
#include "vm/function.h"
#include "core/allocator.h"
#include "core/vec.h"

/**
 * vm_t: 虚拟机上下文
 *
 * 聚合全局状态：当前 scope、全局 scope、调用栈、内置类型注册表。
 * 所有 value 操作需要 vm 作为上下文参数。
 */
typedef struct vm_t {
    allocator_t *alloc;

    /* 作用域 */
    scope_t     *global_scope;  /* 全局根作用域 */
    scope_t     *root_scope;    /* 当前模块作用域（global 的子作用域） */
    scope_t     *current_scope;

    /* ---- 内置类型单例 ---- */
    type_t *type_i8;
    type_t *type_i16;
    type_t *type_i32;
    type_t *type_i64;
    type_t *type_u8;
    type_t *type_u16;
    type_t *type_u32;
    type_t *type_u64;
    type_t *type_f32;
    type_t *type_f64;
    type_t *type_bool;
    type_t *type_str;
    type_t *type_void;
    type_t *type_type;   /* 元类型：type 的 type */
    type_t *type_func;   /* 函数类型基类（无签名） */
    type_t *type_error;  /* 错误类型（引擎级硬错误） */

    /* ---- 函数签名类型池（按签名去重 intern，vm 拥有生命周期） ---- */
    vec_t *sig_types;    /* func_type_t*，元素为签名类型（sig 非空） */
} vm_t;

/** 创建 VM（初始化内置类型、全局作用域、调用栈） */
vm_t *vm_new(allocator_t *alloc);

/** 销毁 VM（释放所有资源） */
void vm_destroy(vm_t **vm);

/** 进入新作用域（current_scope 变为新的子作用域） */
void vm_push_scope(vm_t *vm);

/** 退出当前作用域（销毁该作用域所有 value，current_scope 回退到 parent） */
void vm_pop_scope(vm_t *vm);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_VM_ */
