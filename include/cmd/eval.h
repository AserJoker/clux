#ifndef _H_CLUX_CMD_EVAL_
#define _H_CLUX_CMD_EVAL_
#ifdef __cplusplus
extern "C" {
#endif

#include "cmd/cmd.h"

/**
 * 求值单个表达式字符串（clux eval "<expr>"）。
 *
 * 复用 CTFE 求值器直接解释 AST 产生真实 value，结果按类型打印到 stdout。
 * 错误（词法/语法/求值）输出到 stderr 并返回 1。
 */
int cmd_eval(const cmd_args_t *args);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_CMD_EVAL_ */
