#ifndef _H_CLUX_SEMA_SEMA_
#define _H_CLUX_SEMA_SEMA_
#include "core/allocator.h"
#include "core/arena.h"
#include "core/strslice.h"
#include "core/vec.h"
#include "diag/diagnostic.h"
#include "parser/ast_node.h"
#include "parser/type_qual.h"
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

/* ---- sema 层函数对象 ---- */

/**
 * sema_func_t: 语义分析层的函数单元（统一登记在 sema->funcs 队列）
 *
 * - def: 函数定义 AST 节点（AST_FUNC_DEF，借用，arena 管理，不拥有）
 * - scope: 函数作用域树（Pass 3 构建；局部函数/泛型实例在解析中补建）
 * - name: 函数名（借用 def->name 的 strslice，诊断用）
 *
 * 签名类型不在此持有：函数符号 sema_symbol_t::type 即签名类型
 * （func_type_t，vm 池 intern）。符号表只负责名字解析（sym->ast 指向 def），
 * 函数自身状态（作用域树）由本对象承担——为局部函数提升与泛型单态化
 * 预留：解析过程中发现的新函数（局部函数 / 泛型实例）追加到 sema->funcs
 * 队列末尾，Pass 3 按序处理（队列驱动）。
 */
typedef struct sema_func_t {
    ast_node_t   *def;    /* AST_FUNC_DEF（借用） */
    sema_scope_t *scope;  /* 函数作用域树（Pass 3 填充） */
    strslice_t    name;   /* 函数名（诊断用） */
} sema_func_t;

typedef struct sema_t {
    vm_t         *vm;           /* 复用 VM 类型注册表 + vtable + shadow value */
    diag_buf_t   *diag;         /* 诊断收集器 */
    vec_t        *tokens;       /* token pool（借用，诊断取源码位置） */
    arena_t      *arena;        /* AST 折叠分配（借用 driver arena；comptime
                                   折叠出字面量节点与字符串常量） */
    sema_scope_t *global_scope; /* 全局作用域树根 */

    /* 函数队列：sema 层全部函数（顶层函数 Pass 1 登记；局部函数/泛型实例
       在 Pass 3 解析中追加，队列驱动、可增长）。sema 拥有元素生命周期。 */
    vec_t        *funcs;        /* sema_func_t* */

    /* 函数上下文（Pass 3 walk 时设置） */
    const type_t *func_return_type; /* NULL = void */
    bool          func_has_return;

    /* 循环上下文 */
    int           loop_depth;       /* 0 = 不在循环中 */
} sema_t;

/* ---- 公共 API ---- */

/**
 * 创建 sema 上下文。vm 提供类型注册表/vtable/shadow value；diag 收集诊断；
 * tokens 是 token pool（借用，不拥有），用于把 AST 节点的 tok_begin 下标
 * 解析为源码位置；arena 是 AST 折叠分配器（comptime 折叠用，借用）。
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
sema_t *sema_create(vm_t *vm, diag_buf_t *diag, vec_t *tokens, arena_t *arena);

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

/**
 * 类型表达式求值唯一入口：ast_node_t* → const type_t*
 * (type is expression, m2-design 关键架构决策 6)
 *
 * 类型槽位 = 普通表达式（type is expression）：AST_IDENT 命名类型
 * （type_lookup 沿作用域链查 type value）、AST_CONST/AST_VOLATILE 修饰
 * （递归 sub + intern）。M2 扩展：数组/元组/func 类型表达式与类型计算
 * 等在此求值。
 *
 * 失败返回 NULL（已报错）。空指针入参返回 NULL 不报错（表示"无类型"）。
 */
const type_t *resolve_type_expr(sema_t *sema, ast_node_t *type_expr);

/** 将 AST 节点解析为源码位置（经 token pool）。 */
location_t sema_loc(sema_t *sema, ast_node_t *node);

/** 打印类型名到 stdout（内部调试/诊断用）。 */
void sema_type_name(const type_t *t, char *buf, size_t cap);

/**
 * Pass 3a 作用域树构建（stmt_build.c 实现）：遍历 sema->funcs 队列，对每个函数
 * 按词法块结构建树，只注册符号（名字 + 声明类型 + TDZ 标志），不做类型检查。
 * 作用域树存入 sema_func_t::scope。
 */
void sema_build_scope_tree(sema_t *sema);

/**
 * Pass 3b 单个函数的 shadow VM 运行（stmt.c 实现）。
 * 严格按预建作用域树（sf->scope）遍历函数体，做类型检查与推导。
 */
void sema_walk_function(sema_t *sema, sema_func_t *sf);

/**
 * 表达式求值（shadow value）：只有类型，data=NULL。
 * node 取指针：comptime 折叠（comptime var 引用 / comptime func 调用）会
 * 就地改写 *node 为字面量 AST 节点（arena 分配），下游（编译器）零感知。
 */
value_t *sema_expr(sema_t *sema, ast_node_t **node, sema_scope_t *scope);

/** 检查操作数必须为 bool；error/void shadow（错误恢复产物）静默通过。 */
void sema_check_bool(sema_t *sema, ast_node_t *node, value_t *v,
                     const char *what);

/** 兄弟链节点计数（参数/实参列表长度）。 */
size_t sema_count_siblings(const ast_node_t *node);

#ifdef __cplusplus
}
#endif
#endif
