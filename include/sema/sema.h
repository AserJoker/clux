#ifndef _H_CLUX_SEMA_SEMA_
#define _H_CLUX_SEMA_SEMA_
#include "core/allocator.h"
#include "core/strslice.h"
#include "core/vec.h"
#include "diag/diagnostic.h"
#include "parser/ast_node.h"
#include "sema/symbol.h"
#include "vm/vm.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * 语义分析（sema）
 *
 * 两阶段：先构建 sema 作用域树（scope 节点带完整符号表），再按作用域树
 * 用 shadow value 遍历 AST 做类型检查与推导。作用域结构与类型检查彻底分离。
 *
 * 核心契约：sema 是处理类型错误的最后一个阶段。sema 通过后，字节码编译器
 * 和 VM 可以假定一切类型正确，运行时不再做类型检查。vtable 运算返回的
 * error value 被 sema 捕获并转为诊断，不传播到下游。
 * =========================================================================== */

typedef struct sema_t {
    vm_t         *vm;           /* 复用 VM 类型注册表 + vtable + shadow value */
    diag_buf_t   *diag;         /* 诊断收集器 */
    vec_t        *tokens;       /* token pool（借用，诊断取源码位置） */
    sema_scope_t *global_scope; /* 全局作用域树根 */

    /* 函数上下文（Pass 3b 时设置） */
    const type_t *func_return_type; /* NULL = void */
    bool          func_has_return;

    /* 循环上下文 */
    int           loop_depth;       /* 0 = 不在循环中 */
} sema_t;

/* ---- 公共 API ---- */

/**
 * 创建 sema 上下文。vm 提供类型注册表/vtable/shadow value；diag 收集诊断；
 * tokens 是 token pool（借用，不拥有），用于把 AST 节点的 tok_begin 下标
 * 解析为源码位置。
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
sema_t *sema_create(vm_t *vm, diag_buf_t *diag, vec_t *tokens);

/**
 * 三遍扫描：Pass 1 函数名收集 → Pass 2 类型解析（func_t 签名）→
 * Pass 3a 作用域树构建 + Pass 3b shadow VM 运行（类型检查）。
 *
 * 返回 false 表示存在语义错误（诊断已记录到 diag）。
 * 作用域树在返回后保持有效（持久化数据，交字节码编译器复用）。
 */
bool sema_analyze(sema_t *sema, ast_node_t *program);

/**
 * 销毁 sema 上下文。不销毁作用域树（调用方通过 sema_scope_destroy 释放）
 * 与 token pool / vm / diag（均为借用）。
 * No-op if `sema` or `*sema` is NULL.
 */
void sema_destroy(sema_t **sema);

/* ===========================================================================
 * internal（sema.c / stmt.c 共享，不对外）
 * =========================================================================== */

/** 类型解析唯一入口：M1 内部 type_find；未来替换为类型表达式求值器。 */
const type_t *resolve_type(sema_t *sema, strslice_t name);

/** 将 AST 节点解析为源码位置（经 token pool）。 */
location_t sema_loc(sema_t *sema, ast_node_t *node);

/** 打印类型名到 stdout（内部调试/诊断用）。 */
void sema_type_name(const type_t *t, char *buf, size_t cap);

/**
 * Pass 3a 作用域树构建（stmt.c 实现）：遍历函数体，按词法块结构建树，
 * 只注册符号（名字 + 声明类型 + TDZ 标志），不做类型检查。
 */
void sema_build_scope_tree(sema_t *sema, ast_node_t *program);

/**
 * Pass 3b 单个函数的 shadow VM 运行（stmt.c 实现）。
 * 严格按预建作用域树遍历函数体，做类型检查与推导。
 */
void sema_walk_function(sema_t *sema, ast_node_t *func_def);

/** 表达式求值（shadow value）：只有类型，data=NULL。 */
value_t *sema_expr(sema_t *sema, ast_node_t *node, sema_scope_t *scope);

/** 赋值兼容性：dst 可接受 src 当且仅当 implicit_cast 成功。 */
bool sema_type_assignable(sema_t *sema, const type_t *dst, const type_t *src);

/** 检查操作数必须为 bool；error/void shadow（错误恢复产物）静默通过。 */
void sema_check_bool(sema_t *sema, ast_node_t *node, value_t *v,
                     const char *what);

/** 兄弟链节点计数（参数/实参列表长度）。 */
size_t sema_count_siblings(const ast_node_t *node);

#ifdef __cplusplus
}
#endif
#endif
