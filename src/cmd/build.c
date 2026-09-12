#include "cmd/build.h"

#include <stdio.h>

/* clux build：编译为**机器码二进制**（主线产物）。
 *
 * 这是 build 的主线职责——将来由转译/原生后端（M7 转译 C 后端、M12 原生
 * 编译后端）实现，当前尚未落地。
 *
 * 字节码（.cxb）与汇编文本（.cxs）都是**非主线**的中间/调试产物，不属于
 * build 的范畴，请使用 `clux bc`（字节码工具）。 */

int cmd_build(const cmd_args_t *args) {
    (void)args;
    fprintf(stderr,
            "build: not implemented\n"
            "       build is meant to emit a native machine-code binary\n"
            "       (planned: M7 C backend / M12 native backend).\n"
            "       For bytecode artifacts, use `clux bc`.\n");
    return 1;
}
