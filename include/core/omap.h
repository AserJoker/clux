#ifndef _H_CLUX_CORE_OMAP_
#define _H_CLUX_CORE_OMAP_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include "core/vec.h"
#include <stdbool.h>
#include <stddef.h>

/* ---- Opaque omap type ---- */

typedef struct _omap_t omap_t;

/* ---- Callback signatures ---- */

/**
 * Comparison function for omap keys.
 * Must return:
 *   < 0 if a < b
 *   = 0 if a == b
 *   > 0 if a > b
 */
typedef int (*omap_cmp_fn_t)(const void *a, const void *b);

/* ---- Construction / destruction ---- */

/**
 * Create a new empty ordered map.
 * `cmp_fn` is required and must not be NULL.
 * `owns_key`: if true, keys are freed via allocator_free on omap_free
 *             and cloned via allocator_clone on omap clone.
 * `owns_value`: if true, values are freed/cloned similarly.
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
omap_t *omap_new(allocator_t *allocator, omap_cmp_fn_t cmp_fn,
                 bool owns_key, bool owns_value);

/**
 * Free the ordered map and nullify the caller's pointer.
 * If owns_key/owns_value, calls allocator_free on each key/value.
 * No-op if `map` or `*map` is NULL.
 */
void omap_free(allocator_t *allocator, omap_t **map);

/* ---- Insertion / removal ---- */

/**
 * Insert a key-value pair. If the key already exists, the old value is
 * replaced and returned. The old key is kept; if owns_key is true, the
 * new key is freed (since it is not used).
 * Returns the old value if key existed, NULL otherwise.
 * Panics on out-of-memory. No-op if `map` or `key` is NULL.
 */
void *omap_insert(omap_t *map, allocator_t *allocator, void *key,
                  void *value);

/**
 * Remove the entry with the given key. Returns the removed value,
 * or NULL if not found. The removed key is freed if owns_key is true.
 * The returned value is NOT freed, even if owns_value is true;
 * ownership transfers to the caller.
 */
void *omap_remove(omap_t *map, const void *key);

/* ---- Lookup ---- */

/**
 * Return the value associated with `key`, or NULL if not found.
 */
void *omap_get(const omap_t *map, const void *key);

/**
 * Return true if the map contains `key`.
 */
bool omap_contains(const omap_t *map, const void *key);

/* ---- Properties ---- */

/**
 * Return the number of entries in the map.
 */
size_t omap_size(const omap_t *map);

/**
 * Return true if the map is empty.
 */
bool omap_is_empty(const omap_t *map);

/**
 * Return a read-only view of the key vector, which preserves
 * insertion order. The returned pointer is valid as long as the map
 * is alive and no mutation occurs.
 */
const vec_t *omap_keys(const omap_t *map);

/* ---- Ownership query ---- */

bool omap_owns_key(const omap_t *map);
bool omap_owns_value(const omap_t *map);

#ifdef __cplusplus
}
#endif
#endif
