#include "cmd/run.h"
#include "driver/driver.h"

#include <stdio.h>

/* clux run：运行 clux 程序。
 *
 *   clux run <file.cx>    编译并执行源码
 *   clux run <file.cxb>   加载并执行二进制字节码
 *
 * 二者由**内容**区分：以 "CXBC" 二进制头开头者当字节码执行，否则当源码
 * 编译执行。因此无需任何模式选项——文件叫什么名字、什么扩展名都不影响。
 *
 * `.cxs` 汇编文本既没有二进制头、也不是合法源码，会按其真实内容走源码
 * 编译路径并报编译错误（run 不感知汇编文本这一中间态，见 clux asm）。 */

static const char *USAGE =
    "usage: clux run <file>\n";

int cmd_run(const cmd_args_t *args) {
    if (!args) return 1;

    const char *path = cmd_args_pos(args, 0);
    if (!path) {
        fprintf(stderr, "run: missing input file\n");
        fputs(USAGE, stderr);
        return 1;
    }

    /* 单横线选项（如 -asm）不是 clux 的选项语法：cmd.c 只识别 `--`，
     * 它们会落到位置参数。给出明确提示，避免被误当作文件名。 */
    if (path[0] == '-' && path[1] != '\0') {
        fprintf(stderr,
                "run: unknown option '%s' (clux options use '--')\n", path);
        fputs(USAGE, stderr);
        return 1;
    }

    /* 内容判定：带 CXBC 二进制头 → 字节码执行；否则源码编译执行 */
    if (driver_detect_input(path) == DRIVER_INPUT_CXB) {
        return driver_run_bin(path);
    }
    return driver_run_file(path);
}
