#include "cmd/run.h"
#include "driver/driver.h"
#include <stdio.h>
#include <string.h>

/* 解析 -asm / --asm 模式及其文件路径：
 *   --asm=PATH          （cmd.c 解析为 key="asm", value="PATH"）
 *   --asm PATH          （flag 无 value，路径取自首个位置参数）
 *   -asm PATH           （单短线被 cmd.c 当作位置参数，需在此特判）
 *   -asm=PATH           （同上，位置参数形式）
 * 返回是否进入 asm 模式；*out_path 为解析到的文件路径（可能为 NULL）。 */
static bool resolve_asm_path(const cmd_args_t *args, const char **out_path) {
    const char *p = NULL;

    /* 双短线 --asm / --asm=PATH */
    if (cmd_args_has(args, "asm")) {
        p = cmd_args_get(args, "asm"); /* flag 无值则 NULL */
        if (!p) p = cmd_args_pos(args, 0); /* 回退到紧随的位置参数 */
        *out_path = p;
        return true;
    }

    /* 单短线 -asm PATH / -asm=PATH（cmd.c 仅识别 --，故落入位置参数） */
    for (size_t i = 0; i < args->posc; i++) {
        const char *a = args->posargs[i];
        if (strcmp(a, "-asm") == 0) {
            *out_path = (i + 1 < args->posc) ? args->posargs[i + 1] : NULL;
            return true;
        }
        if (strncmp(a, "-asm=", 5) == 0) {
            *out_path = a + 5;
            return true;
        }
    }

    *out_path = NULL;
    return false;
}

int cmd_run(const cmd_args_t *args) {
    if (!args) return 1;

    const char *asm_path = NULL;
    if (resolve_asm_path(args, &asm_path)) {
        if (!asm_path) {
            fprintf(stderr, "run: -asm requires a file path\n");
            fprintf(stderr, "usage: clux run -asm <file.cxs>\n");
            return 1;
        }
        return driver_run_asm(asm_path);
    }

    /* 常规路径：源码文件为第一个位置参数 */
    const char *path = cmd_args_pos(args, 0);
    if (!path) {
        fprintf(stderr, "run: missing input file\n");
        fprintf(stderr, "usage: clux run <file.cx>\n");
        return 1;
    }

    return driver_run_file(path);
}
