#include "cmd/path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *cmd_derive_out_path(const char *src, const char *ext) {
    const char *slash = strrchr(src, '/');
#ifdef _WIN32
    const char *bslash = strrchr(src, '\\');
    if (bslash > slash) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : src;
    const char *dot = strrchr(base, '.');
    size_t baselen = dot ? (size_t)(dot - base) : strlen(base);
    size_t dirlen = slash ? (size_t)(slash - src) + 1 : 0;
    size_t extlen = strlen(ext);
    char *out = (char *)malloc(dirlen + baselen + extlen + 1);
    if (!out) return NULL;
    if (dirlen) memcpy(out, src, dirlen);
    memcpy(out + dirlen, base, baselen);
    memcpy(out + dirlen + baselen, ext, extlen);
    out[dirlen + baselen + extlen] = '\0';
    return out;
}

int cmd_resolve_output(const cmd_args_t *args, const char *key,
                       const char *in_path, const char *ext,
                       const char **out_path, char **out_owned) {
    *out_owned = NULL;

    /* 显式指定优先：--output=PATH / --output PATH（flag 走位置参数） */
    const char *p = cmd_args_get(args, key);
    if (!p) p = cmd_args_pos(args, 1); /* 位置参数 0 = 输入，1 = 输出 */
    if (p) {
        *out_path = p;
        return 0;
    }

    char *derived = cmd_derive_out_path(in_path, ext);
    if (!derived) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    *out_path = derived;
    *out_owned = derived;
    return 0;
}

int cmd_take_short_output(const cmd_args_t *args,
                          const char **opt_out,
                          const char **out_pos, size_t *out_posn,
                          size_t out_cap) {
    *opt_out = NULL;
    *out_posn = 0;

    for (size_t i = 0; i < args->posc; i++) {
        const char *a = args->posargs[i];
        if (strcmp(a, "-o") == 0) {
            if (i + 1 < args->posc) *opt_out = args->posargs[++i];
            else return 1;   /* -o 缺值 */
            continue;
        }
        if (strncmp(a, "-o=", 3) == 0) {
            *opt_out = a + 3;
            continue;
        }
        if (*out_posn < out_cap) out_pos[(*out_posn)++] = a;
    }
    return 0;
}
