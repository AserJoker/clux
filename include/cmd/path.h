#ifndef _H_CLUX_CMD_PATH_
#define _H_CLUX_CMD_PATH_

#ifdef __cplusplus
extern "C" {
#endif

#include "cmd/cmd.h"

/**
 * 由输入路径推导输出路径：与输入同目录、去原扩展名后加 `ext`。
 *
 * 例：cmd_derive_out_path("a/b/fib.cxs", ".cxb") → "a/b/fib.cxb"
 *
 * 返回新分配的字符串（调用方 free）；内存不足返回 NULL。
 */
char *cmd_derive_out_path(const char *src, const char *ext);

/**
 * 输出路径解析：`args` 中 `key` 选项的 value 优先（如 --output=PATH），
 * 否则用 `cmd_derive_out_path(in_path, ext)` 推导。
 *
 * 成功时 `*out_path` 指向结果（可能是 args 中的字符串，或新分配）；
 * `*out_owned` 为需要 free 的指针（无则为 NULL）。
 *
 * 返回 0 成功；1 失败（内存不足，已打印诊断）。
 */
int cmd_resolve_output(const cmd_args_t *args, const char *key,
                       const char *in_path, const char *ext,
                       const char **out_path, char **out_owned);

#ifdef __cplusplus
}
#endif
#endif
