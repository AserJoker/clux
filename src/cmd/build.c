#include "cmd/build.h"
#include "driver/driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 默认输出路径：与输入同目录、去原扩展名后加 .cxs。 */
static char *derive_asm_path(const char *src) {
    const char *slash = strrchr(src, '/');
#ifdef _WIN32
    const char *bslash = strrchr(src, '\\');
    if (bslash > slash) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : src;
    const char *dot = strrchr(base, '.');
    size_t baselen = dot ? (size_t)(dot - base) : strlen(base);
    size_t dirlen = slash ? (size_t)(slash - src) + 1 : 0;
    const char *ext = ".cxs";
    size_t outlen = dirlen + baselen + strlen(ext) + 1;
    char *out = (char *)malloc(outlen);
    if (!out) return NULL;
    if (dirlen) memcpy(out, src, dirlen);
    memcpy(out + dirlen, base, baselen);
    memcpy(out + dirlen + baselen, ext, strlen(ext));
    out[dirlen + baselen + strlen(ext)] = '\0';
    return out;
}

int cmd_build(const cmd_args_t *args) {
    if (!args) return 1;

    /* 输入文件为第一个位置参数。 */
    const char *path = cmd_args_pos(args, 0);
    if (!path) {
        fprintf(stderr, "build: missing input file\n");
        fprintf(stderr, "usage: clux build <file.cx> [--emit-asm[=PATH]]\n");
        return 1;
    }

    if (!cmd_args_has(args, "emit-asm")) {
        fprintf(stderr,
                "build: nothing to do (use --emit-asm to emit bytecode assembly)\n");
        fprintf(stderr, "usage: clux build <file.cx> [--emit-asm[=PATH]]\n");
        return 1;
    }

    const char *asm_path = cmd_args_get(args, "emit-asm");
    char *derived = NULL;
    if (!asm_path) {
        derived = derive_asm_path(path);
        if (!derived) {
            fprintf(stderr, "build: out of memory\n");
            return 1;
        }
        asm_path = derived;
    }

    int rc = driver_build_asm(path, asm_path);
    if (rc == 0) {
        fprintf(stdout, "wrote %s\n", asm_path);
    }

    if (derived) free(derived);
    return rc;
}
