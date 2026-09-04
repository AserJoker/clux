#ifndef _H_CLUX_CORE_STRMAP_
#define _H_CLUX_CORE_STRMAP_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include "core/vec.h"
#include <stdbool.h>
#include <stddef.h>

/* ---- Opaque strmap type ---- */

typedef struct _strmap_t strmap_t;

/* ---- Construction / destruction ---- */

/**
 * Create a new empty string-keyed ordered map.
 * Keys are always copied and managed internally (strmap owns all keys).
 * If `owns_value` is true, values are freed via allocator_free on strmap_free
 * and cloned via allocator_clone on strmap clone.
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
strmap_t *strmap_new(allocator_t *allocator, bool owns_value);

/**
 * Free the string-keyed map and nullify the caller's pointer.
 * All keys are freed. If owns_value, values are freed as well.
 * No-op if `map` or `*map` is NULL.
 */
void strmap_free(allocator_t *allocator, strmap_t **map);

/* ---- Insertion / removal ---- */

/**
 * Insert a key-value pair. The key string is copied internally.
 * If the key already exists, the old value is replaced and returned.
 * The returned value is NOT freed, even if owns_value is true;
 * ownership transfers to the caller.
 * Panics on out-of-memory. No-op if `map` or `key` is NULL.
 */
void *strmap_insert(strmap_t *map, allocator_t *allocator, const char *key,
                    void *value);

/**
 * Remove the entry with the given key. Returns the removed value,
 * or NULL if not found. The key copy is freed internally.
 * The returned value is NOT freed, even if owns_value is true;
 * ownership transfers to the caller.
 */
void *strmap_remove(strmap_t *map, const char *key);

/* ---- Lookup ---- */

/**
 * Return the value associated with `key`, or NULL if not found.
 */
void *strmap_get(const strmap_t *map, const char *key);

/**
 * Return true if the map contains `key`.
 */
bool strmap_contains(const strmap_t *map, const char *key);

/* ---- Properties ---- */

/**
 * Return the number of entries in the map.
 */
size_t strmap_size(const strmap_t *map);

/**
 * Return true if the map is empty.
 */
bool strmap_is_empty(const strmap_t *map);

/**
 * Return a read-only view of the key vector (const char* elements),
 * preserving insertion order. Valid as long as the map is alive
 * and no mutation occurs.
 */
const vec_t *strmap_keys(const strmap_t *map);

/* ---- Ownership query ---- */

/**
 * Return true if the map owns its values.
 */
bool strmap_owns_value(const strmap_t *map);

#ifdef __cplusplus
}
#endif
#endif
