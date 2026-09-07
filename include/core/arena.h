#ifndef _H_CLUX_CORE_ARENA_
#define _H_CLUX_CORE_ARENA_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include <stddef.h>

/* Portable alignof: C++ has alignof keyword, C11 has _Alignof (via <stdalign.h> alignof macro).
 * Define a short macro so AST node headers can use it in both C and C++ translation units. */
#ifdef __cplusplus
#define ALIGNOF(type) alignof(type)
#else
#include <stdalign.h>
#define ALIGNOF(type) alignof(type)
#endif

/* ---- Arena (bump-pointer region allocator) ---- */

/**
 * A fast, non-tracking allocator for bulk-allocated objects (AST nodes, etc.).
 *
 * Design:
 *   - Bump-pointer allocation within contiguous memory blocks.
 *   - Blocks grow 2x (first block = ARENA_DEFAULT_BLOCK_SIZE).
 *   - No per-allocation header or tracking — minimal overhead.
 *   - No individual free; all memory released at once via `arena_destroy`
 *     or recycled with `arena_reset`.
 *   - OOM triggers panic (consistent with the project convention).
 *
 * All block memory is allocated through the provided `allocator_t`, so arena
 * allocations are tracked by the allocator and leak-detectable.  The arena
 * holds a reference to the allocator for its entire lifetime; the caller must
 * ensure the allocator outlives the arena.
 */
typedef struct _arena_block arena_block_t;
typedef struct _arena_t     arena_t;

/** Default size of the first block (bytes). Subsequent blocks double. */
#define ARENA_DEFAULT_BLOCK_SIZE ((size_t)4096)

/* ---- Construction / destruction ---- */

/**
 * Create a new arena backed by `alloc`. The first block is allocated
 * immediately via `alloc`. Panics on OOM. Returns NULL if `alloc` is NULL
 * or `block_size` is 0.
 */
arena_t *arena_new(allocator_t *alloc, size_t block_size);

/** Convenience: create with ARENA_DEFAULT_BLOCK_SIZE. */
arena_t *arena_new_default(allocator_t *alloc);

/**
 * Release all blocks (via `alloc`) and the arena struct itself.
 * Nullifies `*pa`. No-op if `pa` or `*pa` is NULL.
 */
void arena_destroy(allocator_t *alloc, arena_t **pa);

/**
 * Reset the arena: all existing allocations become invalid, but the
 * memory blocks are retained for reuse (no syscalls on next allocs).
 * Resets the block pointer and bump offset to the beginning.
 */
void arena_reset(arena_t *arena);

/* ---- Allocation ---- */

/**
 * Allocate `size` bytes with `align` alignment from the arena.
 * Panics on OOM or if `size` is 0. Returns a pointer within the
 * current block (fast path) or a new block (slow path).
 *
 * The returned pointer is valid until `arena_destroy` or `arena_reset`.
 */
void *arena_alloc(arena_t *arena, size_t size, size_t align);

/**
 * Allocate `count` elements of `elem_size` bytes each with `align`
 * alignment, zero-initialized. Equivalent to arena_alloc + memset.
 * Panics on overflow, OOM, or if `count`/`elem_size` is 0.
 */
void *arena_calloc(arena_t *arena, size_t count, size_t elem_size, size_t align);

/* ---- Diagnostics ---- */

/** Total number of bytes currently allocated (sum of all block capacities). */
size_t arena_total_allocated(const arena_t *arena);

/** Number of bytes consumed by allocations in the current state. */
size_t arena_total_used(const arena_t *arena);

#ifdef __cplusplus
}
#endif
#endif
