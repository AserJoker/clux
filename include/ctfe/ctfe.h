#ifndef _H_CLUX_CTFE_CTFE_
#define _H_CLUX_CTFE_CTFE_
#ifdef __cplusplus
extern "C" {
#endif

#include "parser/ast_node.h"
#include "sema/sema.h"
#include "vm/vm.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * CTFE（compile-time evaluation）模块
 *
 * 直接解释 AST 产生真实 value（非 shadow），与 sema_expr（shadow 纯类型检查）
 * 平行，仅在编译期常量槽位按需调用（数组边界 N、type 别名计算、
 * sizeof/alignof/typeof、enum 值）。eval 命令亦复用本模块做端到端求值。
 *
 * 严格限制（设计定稿，docs/m2-design.md §8）：
 *   - 同步递归 AST 解释器，不允许暂停恢复
 *   - 必须编译期求值：遇运行期依赖（scope 查不到的标识符等）→ error
 *   - 禁止 FFI：只解释 AST_FUNC_DEF（clux 函数）；非 AST 实体不在可调范围
 *     （本骨架不实现 FFI 识别，规则仅记录）
 *
 * 生命周期契约：ctfe_eval / ctfe_eval_stmt 产生的中间值与结果都 track 到
 * vm->current_scope。调用方负责：push scope → 求值 → clone 结果到自己的
 * scope（若需跨 scope 存活）→ pop scope。
 */

/* ---- 控制流标志（语句解释器内部使用） ---- */

typedef enum {
    CTFE_CTRL_NONE = 0,
    CTFE_CTRL_RETURN,    /* return 触发：函数体解释立即停止 */
    CTFE_CTRL_BREAK,     /* break 触发 */
    CTFE_CTRL_CONTINUE,  /* continue 触发 */
} ctfe_ctrl_t;

/* ---- 求值上下文 ---- */

typedef struct ctfe_ctx {
    vm_t        *vm;        /* 求值上下文：value 构造/运算 + scope 生命周期 */
    sema_t      *sema;      /* 符号表（函数调用查 AST_FUNC_DEF）；可 NULL（eval 场景） */
    size_t       budget;    /* 剩余求值步数（每节点 -1，耗尽报错，防死循环） */
    size_t       depth;     /* 当前递归深度（ctfe_eval 嵌套层数） */
    size_t       max_depth; /* 递归深度上限（函数嵌套/表达式嵌套） */
    ctfe_ctrl_t  ctrl;      /* 控制流标志（RETURN/BREAK/CONTINUE，语句解释器读写） */
    value_t     *ret_value; /* RETURN 时携带的返回值（借用，归 callee scope） */
} ctfe_ctx_t;

/* ---- 公共 API ---- */

/**
 * 表达式求值：返回真实 value（track 到 vm->current_scope）。
 * 求值失败返回 error value（value_is_error 判定）。
 * NULL 参数 / 未知节点种类 → error value。
 */
value_t *ctfe_eval(ctfe_ctx_t *ctx, ast_node_t *node);

/**
 * 语句解释：逐条执行块内语句。遇 AST_RETURN / AST_BREAK / AST_CONTINUE
 * 设置 ctx->ctrl 并立即返回（块解释循环据此停止）。
 * 表达式节点作为语句传入时求值并丢弃结果。
 * 失败返回 error value（value_is_error 判定）。
 */
value_t *ctfe_eval_stmt(ctfe_ctx_t *ctx, ast_node_t *stmt);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_CTFE_CTFE_ */
