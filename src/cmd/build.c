#include "cmd/build.h"
#include "driver/driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 默认输出路径：与输入同目录、去原扩展名后加指定扩展名。 */
static char *derive_out_path(const char *src, const char *ext) {
    const char *slash = strrchr(src, '/');
#ifdef _WIN32
    const char *bslash = strrchr(src, '\\');
    if (bslash > slash) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : src;
    const char *dot = strrchr(base, '.');
    size_t baselen = dot ? (size_t)(dot - base) : strlen(base);
    size_t dirlen = slash ? (size_t)(slash - src) + 1 : 0;
    size_t outlen = dirlen + baselen + strlen(ext) + 1;
    char *out = (char *)malloc(outlen);
    if (!out) return NULL;
    if (dirlen) memcpy(out, src, dirlen);
    memcpy(out + dirlen, base, baselen);
    memcpy(out + dirlen + baselen, ext, strlen(ext));
    out[dirlen + baselen + strlen(ext)] = '\0';
    return out;
}

/* 解析输入类型：优先 --input=<cx|cxs|cxb> 显式指定，否则内容嗅探。 */
static driver_input_kind_t resolve_input_kind(const cmd_args_t *args,
                                              const char *path,
                                              bool *out_explicit) {
    *out_explicit = false;
    const char *forced = cmd_args_get(args, "input");
    if (forced) {
        *out_explicit = true;
        if (strcmp(forced, "cx") == 0)   return DRIVER_INPUT_SOURCE;
        if (strcmp(forced, "cxs") == 0)  return DRIVER_INPUT_CXS;
        if (strcmp(forced, "cxb") == 0)  return DRIVER_INPUT_CXB;
        return DRIVER_INPUT_UNKNOWN;
    }
    return driver_detect_input(path);
}

/* 输出路径：显式 value 优先，否则按输入推导（换扩展名）。
 * 推导出的路径经 *out_owned 交回，调用方用 free 释放。 */
static int resolve_out_path(const cmd_args_t *args, const char *key,
                            const char *in_path, const char *ext,
                            const char **out_path, char **out_owned) {
    *out_owned = NULL;
    const char *p = cmd_args_get(args, key);
    if (!p) {
        char *derived = derive_out_path(in_path, ext);
        if (!derived) {
            fprintf(stderr, "build: out of memory\n");
            return 1;
        }
        *out_owned = derived;
        p = derived;
    }
    *out_path = p;
    return 0;
}

static const char *USAGE =
    "usage: clux build <file> [options]\n"
    "\n"
    "Source compile (input is clux source .cx):\n"
    "  --emit-asm[=PATH]   compile and emit .cxs text assembly\n"
    "  --emit-bin[=PATH]   compile and emit .cxb binary bytecode\n"
    "\n"
    "Format conversion (no source front end):\n"
    "  --to-bin[=PATH]     .cxs text  -> .cxb binary\n"
    "  --to-asm[=PATH]     .cxb binary -> .cxs text\n"
    "\n"
    "Other:\n"
    "  --input=<cx|cxs|cxb>  force input kind (skip content sniffing)\n";

int cmd_build(const cmd_args_t *args) {
    if (!args) return 1;

    const char *path = cmd_args_pos(args, 0);
    if (!path) {
        fprintf(stderr, "build: missing input file\n");
        fputs(USAGE, stderr);
        return 1;
    }

    /* 单横线选项（如 -asm/-bin）不是 clux 的选项语法：cmd.c 只识别 `--`，
     * 它们会落到位置参数。给出明确提示，避免被误当作文件名。 */
    if (path[0] == '-' && path[1] != '\0') {
        fprintf(stderr,
                "build: unknown option '%s' (clux options use '--')\n", path);
        fputs(USAGE, stderr);
        return 1;
    }

    bool emit_asm = cmd_args_has(args, "emit-asm");
    bool emit_bin = cmd_args_has(args, "emit-bin");
    bool to_bin   = cmd_args_has(args, "to-bin");
    bool to_asm   = cmd_args_has(args, "to-asm");

    if (!emit_asm && !emit_bin && !to_bin && !to_asm) {
        fprintf(stderr, "build: nothing to do\n");
        fputs(USAGE, stderr);
        return 1;
    }

    /* 编译与互转语义不同，禁止混用，避免歧义。 */
    if ((to_bin || to_asm) && (emit_asm || emit_bin)) {
        fprintf(stderr,
                "build: --to-bin/--to-asm cannot be combined with --emit-asm/--emit-bin\n");
        fputs(USAGE, stderr);
        return 1;
    }

    /* 统一解析输入类型（--input 显式 → 否则内容嗅探）。 */
    bool explicit_kind = false;
    driver_input_kind_t kind = resolve_input_kind(args, path, &explicit_kind);
    if (cmd_args_has(args, "input") && kind == DRIVER_INPUT_UNKNOWN) {
        fprintf(stderr,
                "build: invalid --input value (expected cx, cxs or cxb)\n");
        fputs(USAGE, stderr);
        return 1;
    }

    int rc = 0;
    char *owned = NULL;
    const char *out = NULL;

    /* ---- 源码编译路径 ---- */
    if (emit_asm || emit_bin) {
        if (kind != DRIVER_INPUT_SOURCE) {
            fprintf(stderr,
                    "build: --emit-asm/--emit-bin expects clux source, but input "
                    "looks like %s\n",
                    driver_input_kind_name(kind));
            if (kind == DRIVER_INPUT_UNKNOWN) {
                fprintf(stderr,
                        "       (use --input=cx to force source, or --to-* for "
                        "format conversion)\n");
            }
            return 1;
        }

        if (emit_asm) {
            if (resolve_out_path(args, "emit-asm", path, ".cxs", &out, &owned)) return 1;
            rc = driver_build_asm(path, out);
            if (rc == 0) fprintf(stdout, "wrote %s\n", out);
            free(owned);
            if (rc != 0) return rc;
        }
        if (emit_bin) {
            if (resolve_out_path(args, "emit-bin", path, ".cxb", &out, &owned)) return 1;
            rc = driver_build_bin(path, out);
            if (rc == 0) fprintf(stdout, "wrote %s\n", out);
            free(owned);
            if (rc != 0) return rc;
        }
        return 0;
    }

    /* ---- 格式互转路径（方向由嗅探决定） ---- */
    if (to_bin) {
        if (kind != DRIVER_INPUT_CXS) {
            fprintf(stderr,
                    "build: --to-bin expects .cxs assembly text, but input looks "
                    "like %s\n", driver_input_kind_name(kind));
            if (kind == DRIVER_INPUT_UNKNOWN)
                fprintf(stderr, "       (use --input=cxs to force)\n");
            return 1;
        }
        if (resolve_out_path(args, "to-bin", path, ".cxb", &out, &owned)) return 1;
        rc = driver_asm_to_bin(path, out);
        if (rc == 0) fprintf(stdout, "wrote %s\n", out);
        free(owned);
        return rc;
    }

    if (to_asm) {
        if (kind != DRIVER_INPUT_CXB) {
            fprintf(stderr,
                    "build: --to-asm expects .cxb binary, but input looks like %s\n",
                    driver_input_kind_name(kind));
            if (kind == DRIVER_INPUT_UNKNOWN)
                fprintf(stderr, "       (use --input=cxb to force)\n");
            return 1;
        }
        if (resolve_out_path(args, "to-asm", path, ".cxs", &out, &owned)) return 1;
        rc = driver_bin_to_asm(path, out);
        if (rc == 0) fprintf(stdout, "wrote %s\n", out);
        free(owned);
        return rc;
    }

    return 0;
}
