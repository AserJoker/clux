#ifndef _H_CLUX_CORE_RBTREE_
#define _H_CLUX_CORE_RBTREE_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include <stdbool.h>
#include <stddef.h>

/* ---- Opaque rbtree type ---- */

typedef struct _rbtree_t rbtree_t;

/* ---- Callback signatures ---- */

/**
 * Comparison function for rbtree elements.
 * Must return:
 *   < 0 if a < b
 *   = 0 if a == b
 *   > 0 if a > b
 */
typedef int (*rbtree_cmp_fn_t)(const void *a, const void *b);

/* ---- Construction / destruction ---- */

/**
 * Create a new empty red-black tree.
 * `cmp_fn` is required and must not be NULL (panics otherwise).
 * If `owns_element` is true, rbtree_free will call allocator_free on each
 * element, and rbtree_clone will call allocator_clone on each element.
 * Panics on out-of-memory. Returns NULL for invalid arguments.
 */
rbtree_t *
rbtree_new(allocator_t *allocator, rbtree_cmp_fn_t cmp_fn, bool owns_element);

/**
 * Free the rbtree and nullify the caller's pointer.
 * If owns_element is true, calls allocator_free on each element first.
 * No-op if `tree` or `*tree` is NULL.
 */
void rbtree_free(allocator_t *allocator, rbtree_t **tree);

/* ---- Insertion / removal ---- */

/**
 * Insert `element` into the tree. If an equal element already exists
 * (cmp_fn returns 0), the old element is replaced and returned.
 * Otherwise returns NULL.
 * Panics on out-of-memory. No-op if `tree` or `element` is NULL.
 */
void *rbtree_insert(rbtree_t *tree, allocator_t *allocator, void *element);

/**
 * Remove the element equal to `key` (per cmp_fn) from the tree.
 * Returns the removed element, or NULL if not found.
 * The returned pointer is NOT freed, even if owns_element is true;
 * ownership transfers to the caller.
 */
void *rbtree_remove(rbtree_t *tree, const void *key);

/* ---- Lookup ---- */

/**
 * Return the element equal to `key`, or NULL if not found.
 */
void *rbtree_find(const rbtree_t *tree, const void *key);

/**
 * Return true if the tree contains an element equal to `key`.
 */
bool rbtree_contains(const rbtree_t *tree, const void *key);

/* ---- Properties ---- */

/**
 * Return the number of elements in the tree.
 */
size_t rbtree_size(const rbtree_t *tree);

/**
 * Return true if the tree contains no elements.
 */
bool rbtree_is_empty(const rbtree_t *tree);

/**
 * Return the minimum element (leftmost), or NULL if empty.
 */
void *rbtree_min(const rbtree_t *tree);

/**
 * Return the maximum element (rightmost), or NULL if empty.
 */
void *rbtree_max(const rbtree_t *tree);

/* ---- Ownership query ---- */

/**
 * Return true if the tree owns its elements.
 */
bool rbtree_owns_element(const rbtree_t *tree);

/* ---- Traversal ---- */

/**
 * Visitor callback for rbtree_foreach. Called for each element in sorted order.
 * `element` is the stored pointer; `ctx` is the user-provided context.
 */
typedef void (*rbtree_visit_fn_t)(void *element, void *ctx);

/**
 * Visit every element in the tree in sorted (in-order) order.
 * No-op if `tree` or `visit` is NULL.
 */
void rbtree_foreach(const rbtree_t *tree, rbtree_visit_fn_t visit, void *ctx);

#ifdef __cplusplus
}
#endif
#endif
