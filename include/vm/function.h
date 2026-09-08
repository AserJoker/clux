#ifndef _H_CLUX_VM_FUNCTION_
#define _H_CLUX_VM_FUNCTION_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/value.h"
#include "core/strslice.h"
#include <stddef.h>

typedef struct vm_t    vm_t;
typedef struct scope_t scope_t;
typedef struct func_t  func_t;

/* ---- C 函数签名 ---- */

/**
 * 引擎侧函数原型：所有 clux 函数最终以 C 函数形式执行。
 * - vm: 虚拟机上下文
 * - self: 指向 func_t 自身
 * - argc: 实参数量
 * - args: 实参数组（已 clone 到当前作用域）
 * 返回值: 结果 value（调用方负责 clone 到自己的作用域）
 */
typedef value_t *(*cfunc_t)(vm_t *vm, func_t *self, size_t argc, value_t **args);

/* ---- func_t: 函数对象 ---- */

/**
 * func_t: 函数值载荷
 *
 * - cfunc: C 函数指针，引擎通过它执行逻辑
 * - closure_scope: 定义时的词法环境（闭包捕获）
 * - root_scope: 定义时的模块作用域（全局根的子作用域）
 * - return_type: 返回值类型，NULL 表示 void
 * - name: 函数名（调试/显示用）
 *
 * 引擎视角的参数没有名字，cfunc 通过 args[i] 按位置访问。
 * 参数名绑定是语言层（AST interpreter）的职责。
 */
struct func_t {
    cfunc_t         cfunc;
    scope_t        *closure_scope;
    scope_t        *root_scope;
    const type_t  **params;        /* 参数类型数组，按位置 */
    size_t          param_count;
    const type_t   *return_type;
    strslice_t      name;
};

/* ---- 生命周期 ---- */

/** 创建函数对象 */
func_t *func_new(allocator_t *alloc,
                  cfunc_t cfunc,
                  scope_t *closure_scope,
                  scope_t *root_scope,
                  strslice_t name);

/** 销毁函数对象 */
void func_destroy(allocator_t *alloc, func_t **fn);

/** 设置返回类型 */
void func_set_return_type(func_t *fn, const type_t *type);

/* ---- value 构造 ---- */

/** 从 func_t 构造 value_t（堆分配 + auto-track，data 存指向 func_t 的指针） */
value_t *func_make_value(vm_t *vm, func_t *fn);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_FUNCTION_ */
