#ifndef _H_CLUX_VM_FUNCTION_
#define _H_CLUX_VM_FUNCTION_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/value.h"
#include "core/strslice.h"
#include <stdbool.h>
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
 * func_t: 函数对象（VM 引擎侧）
 *
 * - cfunc: C 函数指针，引擎通过它执行逻辑
 * - closure_scope: 定义时的词法环境（闭包捕获）
 * - root_scope: 定义时的模块作用域（全局根的子作用域）
 * - name: 函数名（调试/显示用）
 *
 * 签名不存于 func_t：签名类型（func_type_t）由 func_new 传入并成为
 * func value 的 type，调用点经 value_type() 取回（见 func_new）。
 * sema 侧不构造 func value：符号表只记录函数定义 AST 节点（sym->ast），
 * 签名类型存于 sym->type。
 *
 * 引擎视角的参数没有名字，cfunc 通过 args[i] 按位置访问。
 * 参数名绑定是语言层（AST interpreter）的职责。
 */
struct func_t {
    cfunc_t         cfunc;
    scope_t        *closure_scope;
    scope_t        *root_scope;
    strslice_t      name;
};

/* ---- 生命周期 ---- */

/**
 * 创建函数对象并包装为 func value。
 *
 * - sig_type: 签名类型（func_type_t，由 type_func_sig 注册 intern）。
 *   成为 func value 的 type：调用点经 value_type() 取回签名，
 *   func_shadow_call 据此做参数数量/隐式转换校验。不可为 NULL。
 * - 返回 value_t*：data 存指向 func_t 的指针，type 即 sig_type。
 *   untracked（调用方管理生命周期）：value_dispose(vm, v) 释放 data 内
 *   func_t 与 data 块，再 allocator_free(alloc, &v)。
 */
value_t *func_new(allocator_t *alloc,
                  cfunc_t cfunc,
                  scope_t *closure_scope,
                  scope_t *root_scope,
                  const type_t *sig_type,
                  strslice_t name);

/** 销毁函数对象（释放 func_t 自身；签名归 vm 类型池所有） */
void func_destroy(allocator_t *alloc, func_t **fn);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_FUNCTION_ */
