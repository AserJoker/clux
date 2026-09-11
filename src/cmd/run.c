#include "cmd/run.h"
#include "driver/driver.h"
#include <stdio.h>
#include <string.h>

/* 取选项路径：`--key=PATH` 取 value；`--key PATH`（flag）回退到首个位置参数。 */
static const char *opt_path(const cmd_args_t *args, const char *key) {
    const char *p = cmd_args_get(args, key);
    if (!p) p = cmd_args_pos(args, 0);
    return p;
}

static const char *USAGE =
    "usage: clux run <file.cx>\n"
    "       clux run --asm <file.cxs>\n"
    "       clux run --bin <file.cxb>\n";

int cmd_run(const cmd_args_t *args) {
    if (!args) return 1;

    bool asm_mode = cmd_args_has(args, "asm");
    bool bin_mode = cmd_args_has(args, "bin");

    if (asm_mode && bin_mode) {
        fprintf(stderr, "run: --asm and --bin are mutually exclusive\n");
        fputs(USAGE, stderr);
        return 1;
    }

    /* .cxs 文本汇编模式 */
    if (asm_mode) {
        const char *path = opt_path(args, "asm");
        if (!path) {
            fprintf(stderr, "run: --asm requires a file path\n");
            fputs(USAGE, stderr);
            return 1;
        }
        return driver_run_asm(path);
    }

    /* .cxb 二进制字节码模式 */
    if (bin_mode) {
        const char *path = opt_path(args, "bin");
        if (!path) {
            fprintf(stderr, "run: --bin requires a file path\n");
            fputs(USAGE, stderr);
            return 1;
        }
        return driver_run_bin(path);
    }

    /* 常规路径：源码文件为第一个位置参数 */
    const char *path = cmd_args_pos(args, 0);
    if (!path) {
        fprintf(stderr, "run: missing input file\n");
        fputs(USAGE, stderr);
        return 1;
    }

    /* 单横线选项（如 -asm/-bin）不是 clux 的选项语法：cmd.c 只识别 `--`，
     * 它们会落到位置参数。给出明确提示，避免被误当作文件名。 */
    if (path[0] == '-' && path[1] != '\0') {
        fprintf(stderr,
                "run: unknown option '%s' (clux options use '--', e.g. --asm/--bin)\n",
                path);
        fputs(USAGE, stderr);
        return 1;
    }

    return driver_run_file(path);
}
