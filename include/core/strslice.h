#ifndef _H_CLUX_CORE_STRSLICE_
#define _H_CLUX_CORE_STRSLICE_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Non-owning string slice ---- */

/**
 * A lightweight, non-owning view into a contiguous character sequence.
 * Unlike `string_t` (heap-allocated, owning), `strslice_t` borrows a pointer
 * and is safe to copy by value.  It is the primary string type used in the
 * lexer, parser, and AST for zero-copy token text and identifier names.
 */
typedef struct strslice {
    const char *ptr;
    size_t      len;
} strslice_t;

/* ---- Constructor macros ---- */

/** Empty slice (NULL pointer, zero length). */
#define STRSLICE_EMPTY ((strslice_t){ .ptr = NULL, .len = 0 })

/**
 * Construct a slice from a string literal (compile-time).
 * Usage: STRSLICE_LIT("hello")  — length is computed by sizeof, which
 * includes the NUL terminator; the macro subtracts 1.
 */
#define STRSLICE_LIT(s) ((strslice_t){ .ptr = (s), .len = sizeof(s) - 1 })

/* ---- Inline helpers ---- */

/** Return true if the slice has zero length. */
static inline bool strslice_is_empty(strslice_t s) {
    return s.len == 0;
}

/** Byte-wise equality.  NULL pointers compare equal if both are empty. */
static inline bool strslice_eq(strslice_t a, strslice_t b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

/** Lexicographic comparison (< 0, == 0, > 0). */
static inline int strslice_cmp(strslice_t a, strslice_t b) {
    size_t min_len = a.len < b.len ? a.len : b.len;
    if (min_len > 0) {
        int r = memcmp(a.ptr, b.ptr, min_len);
        if (r != 0) return r;
    }
    if (a.len < b.len) return -1;
    if (a.len > b.len) return 1;
    return 0;
}

/** FNV-1a hash for use in hash tables (strmap, etc.). */
static inline unsigned long strslice_hash(strslice_t s) {
    unsigned long h = 14695981039346656037UL;
    for (size_t i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.ptr[i];
        h *= 1099511628211UL;
    }
    return h;
}

/** Construct a slice from a NUL-terminated C string. */
static inline strslice_t strslice_from_cstr(const char *cstr) {
    if (!cstr) return STRSLICE_EMPTY;
    return (strslice_t){ .ptr = cstr, .len = strlen(cstr) };
}

/** Construct a slice from `len` bytes starting at `data`. */
static inline strslice_t strslice_from_bytes(const char *data, size_t len) {
    return (strslice_t){ .ptr = data, .len = len };
}

/** Return a sub-slice [start, start+length), clamped to bounds. */
static inline strslice_t strslice_substr(strslice_t s, size_t start, size_t length) {
    if (start >= s.len) return STRSLICE_EMPTY;
    size_t avail = s.len - start;
    if (length > avail) length = avail;
    return (strslice_t){ .ptr = s.ptr + start, .len = length };
}

/** Return true if `s` starts with `prefix`. */
static inline bool strslice_starts_with(strslice_t s, strslice_t prefix) {
    if (prefix.len > s.len) return false;
    if (prefix.len == 0) return true;
    return memcmp(s.ptr, prefix.ptr, prefix.len) == 0;
}

/** Return true if `s` ends with `suffix`. */
static inline bool strslice_ends_with(strslice_t s, strslice_t suffix) {
    if (suffix.len > s.len) return false;
    if (suffix.len == 0) return true;
    return memcmp(s.ptr + s.len - suffix.len, suffix.ptr, suffix.len) == 0;
}

#ifdef __cplusplus
}
#endif
#endif
