#ifndef _H_CLUX_CMD_BC_
#define _H_CLUX_CMD_BC_

#ifdef __cplusplus
extern "C" {
#endif

#include "cmd/cmd.h"

/**
 * clux bc：字节码工具（非主线，调试/分发用）。
 *
 *   clux bc emit   <file.cx>  [-o PATH]   源码 → .cxb 二进制字节码
 *   clux bc asm    <file.cxs> [-o PATH]   .cxs 汇编文本 → .cxb 二进制
 *   clux bc disasm <file.cxb> [-o PATH]   .cxb 二进制 → .cxs 汇编文本
 *
 * 输出路径省略时按输入同名换扩展名。
 */
int cmd_bc(const cmd_args_t *args);

#ifdef __cplusplus
}
#endif
#endif
