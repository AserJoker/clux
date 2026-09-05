#ifndef _H_CLUX_CORE_STRING_
#define _H_CLUX_CORE_STRING_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include <stdbool.h>

/* ---- Opaque string type ---- */

typedef struct _string_t string_t;

/** Sentinel returned by search functions when no match is found. */
#define STRING_NPOS ((size_t)-1)

/* ---- Construction / destruction ---- */

/**
 * Create a new empty string. The string keeps a reference to `allocator`
 * and uses it for all internal allocations; no further allocator arguments
 * are needed on mutating operations.
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
string_t *string_new(allocator_t *allocator);

/**
 * Create a string from a NUL-terminated C string (copied).
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
string_t *string_from_cstr(allocator_t *allocator, const char *cstr);

/**
 * Create a string from `len` raw bytes (copied). The result is always
 * NUL-terminated, but embedded NUL bytes are preserved.
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
string_t *
string_from_bytes(allocator_t *allocator, const char *data, size_t len);

/**
 * Create an independent copy of `other`.
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
string_t *string_from_string(allocator_t *allocator, const string_t *other);

/**
 * Free the string and nullify the caller's pointer.
 * Uses the allocator stored inside the string.
 * No-op if `str` or `*str` is NULL.
 */
void string_free(string_t **str);

/* ---- Accessors ---- */

/**
 * Return the contents as a NUL-terminated C string (read-only).
 * The pointer is valid until the next mutating operation on `str`.
 * An empty string yields "" (a static literal).
 */
const char *string_cstr(const string_t *str);

/** Alias for string_cstr. */
const char *string_data(const string_t *str);

/** Return the byte length of the string (excluding the NUL terminator). */
size_t string_len(const string_t *str);

/** Return the allocated capacity in bytes (includes the NUL slot). */
size_t string_cap(const string_t *str);

/** Return true if the string has length 0. */
bool string_is_empty(const string_t *str);

/**
 * Return the byte at `index` as an unsigned char, or -1 if out of bounds.
 */
int string_char_at(const string_t *str, size_t index);

/* ---- Mutation ---- */

/**
 * Append a NUL-terminated C string to the end. Grows the buffer if needed.
 * Panics on out-of-memory. No-op if `str` or `cstr` is NULL.
 */
void string_append_cstr(string_t *str, const char *cstr);

/**
 * Append `len` raw bytes to the end. Grows the buffer if needed.
 * Panics on out-of-memory. No-op if `str` or `data` is NULL, or len == 0.
 */
void string_append_bytes(string_t *str, const char *data, size_t len);

/** Append a single byte. Panics on out-of-memory. No-op if `str` is NULL. */
void string_append_char(string_t *str, char ch);

/** Append the contents of `other`. Panics on out-of-memory. No-op if NULL. */
void string_append_string(string_t *str, const string_t *other);

/** Replace the contents with a copy of `cstr`. No-op if `str` or `cstr` is NULL. */
void string_assign_cstr(string_t *str, const char *cstr);

/** Replace the contents with a copy of `len` raw bytes. No-op if NULL. */
void string_assign_bytes(string_t *str, const char *data, size_t len);

/** Reset the string to empty, keeping the allocated capacity. */
void string_clear(string_t *str);

/**
 * Ensure capacity for at least `additional` more bytes beyond the current
 * length. May reallocate. Panics on capacity overflow / out-of-memory.
 * No-op if `str` is NULL or capacity is already sufficient.
 */
void string_reserve(string_t *str, size_t additional);

/**
 * Shrink the buffer to fit the current length. Panics on out-of-memory.
 * No-op if `str` is NULL or the buffer already fits.
 */
void string_shrink_to_fit(string_t *str);

/* ---- Searching ---- */

/**
 * Find the first occurrence of `needle` at or after byte offset `start`.
 * Returns the byte offset, or STRING_NPOS if not found.
 * An empty needle matches at `start` (C++ semantics).
 */
size_t string_find(const string_t *str, const char *needle, size_t start);

/**
 * Find the last occurrence of `needle`.
 * Returns the byte offset, or STRING_NPOS if not found.
 * An empty needle matches at the end of the string.
 */
size_t string_rfind(const string_t *str, const char *needle);

/** Return true if `needle` occurs anywhere in `str`. */
bool string_contains(const string_t *str, const char *needle);

/** Return true if `str` begins with `prefix`. */
bool string_starts_with(const string_t *str, const char *prefix);

/** Return true if `str` ends with `suffix`. */
bool string_ends_with(const string_t *str, const char *suffix);

/* ---- Comparison ---- */

/**
 * Compare two strings byte-wise (strcmp semantics): < 0, == 0, or > 0.
 * NULL sorts before any non-NULL string; NULL vs NULL is 0.
 */
int string_compare(const string_t *a, const string_t *b);

/** Return true if both strings have identical length and contents. */
bool string_equals(const string_t *a, const string_t *b);

/* ---- Derived strings (return a new string, original untouched) ---- */

/**
 * Return a new string holding the `len` bytes of `str` starting at `start`.
 * `len` is clamped to the available bytes. Returns NULL if out of bounds
 * or on invalid arguments.
 */
string_t *string_substring(allocator_t *allocator,
                           const string_t *str,
                           size_t start,
                           size_t len);

/**
 * Return a new string that is the concatenation of `a` and `b`.
 * Panics on length overflow / out-of-memory. Returns NULL for invalid args.
 */
string_t *
string_concat(allocator_t *allocator, const string_t *a, const string_t *b);

/**
 * Return a new string with the first occurrence of `needle` replaced by
 * `replacement` (NULL means empty, i.e. delete the match).
 * Returns a copy of `str` if `needle` is empty or not found.
 * Returns NULL for invalid arguments (allocator/str/needle NULL).
 * Panics on length overflow / out-of-memory.
 */
string_t *string_replace(allocator_t *allocator,
                         const string_t *str,
                         const char *needle,
                         const char *replacement);

/**
 * Return a new string with every occurrence of `needle` replaced by
 * `replacement` (NULL means empty, i.e. delete the matches).
 * Returns a copy of `str` if `needle` is empty or not found.
 * Returns NULL for invalid arguments (allocator/str/needle NULL).
 * Panics on length overflow / out-of-memory.
 */
string_t *string_replace_all(allocator_t *allocator,
                             const string_t *str,
                             const char *needle,
                             const char *replacement);

#ifdef __cplusplus
}
#endif
#endif
