#include "core/arena.h"
#include "core/panic.h"

#include <stdlib.h>
#include <string.h>

/* ---- Block: a contiguous memory region within the arena ---- */

struct _arena_block {
    arena_block_t *next;   /* linked list of blocks */
    size_t         cap;    /* usable capacity (bytes, excludes this header) */
    size_t         used;   /* bytes already bump-allocated from this block */
    /* user data starts at (arena_block_t*) + 1 */
};

/* ---- Arena ---- */

struct _arena_t {
    allocator_t   *alloc;       /* backing allocator for block memory */
    arena_block_t *blocks;      /* head of block list (first = smallest) */
    size_t         next_size;   /* size of the next block to allocate */
    size_t         total_used;  /* sum of used across all blocks */
};

/* ---- Internal: class for arena blocks (allocated via allocator_t) ---- */

static class_t g_arena_block_class = {
    .name = "clux.arena.block",
    .size = sizeof(char), /* element size = 1 byte; count = capacity */
    .clone_fn = NULL,
    .move_fn = NULL,
    .dispose_fn = NULL,
};

/* ---- Internal helpers ---- */

/* Alignment helper: round up `val` to the nearest multiple of `align`. */
static inline size_t align_up(size_t val, size_t align) {
    return (val + align - 1) & ~(align - 1);
}

/* Return the pointer to user data within a block. */
static inline char *block_data(arena_block_t *blk) {
    return (char *)(blk + 1);
}

/* Allocate a new block of at least `min_bytes` usable capacity via allocator. */
static arena_block_t *block_new(allocator_t *alloc, size_t min_bytes) {
    size_t cap = min_bytes < sizeof(arena_block_t)
                     ? sizeof(arena_block_t)
                     : min_bytes;
    /*
     * We need: sizeof(arena_block_t) header + cap bytes of user data.
     * allocator_new_ex adds its own tracking header internally; we
     * allocate a single blob large enough for both the arena_block_t
     * header and the user data, then cast.
     *
     * Use allocator_new_ex with count = sizeof(arena_block_t) + cap
     * so the raw allocation is large enough for the header + data.
     */
    size_t total_bytes = sizeof(arena_block_t) + cap;
    char *raw = (char *)allocator_new_ex(
        alloc,
        g_arena_block_class.name,
        /*size=*/1,
        /*move_fn=*/NULL,
        /*clone_fn=*/NULL,
        /*dispose_fn=*/NULL,
        /*count=*/total_bytes);
    if (!raw) panic("arena: out of memory");

    arena_block_t *blk = (arena_block_t *)raw;
    blk->next = NULL;
    blk->cap  = cap;
    blk->used = 0;
    return blk;
}

/* Free a block via allocator. */
static void block_free(allocator_t *alloc, arena_block_t *blk) {
    /* The block was allocated as a single allocator_new_ex blob;
     * free the base pointer. */
    void *p = (void *)blk;
    allocator_free(alloc, &p);
}

/* ---- Public API ---- */

arena_t *arena_new(allocator_t *alloc, size_t block_size) {
    if (!alloc || block_size == 0) return NULL;

    /* Allocate the arena struct itself via allocator. */
    arena_t *a = (arena_t *)allocator_new_ex(
        alloc,
        "clux.arena",
        sizeof(char),
        NULL, NULL, NULL,
        sizeof(arena_t));
    if (!a) panic("arena: out of memory");

    a->alloc      = alloc;
    a->blocks     = block_new(alloc, block_size);
    a->next_size  = block_size * 2;
    a->total_used = 0;
    return a;
}

arena_t *arena_new_default(allocator_t *alloc) {
    return arena_new(alloc, ARENA_DEFAULT_BLOCK_SIZE);
}

void arena_destroy(allocator_t *alloc, arena_t **pa) {
    if (!pa || !*pa) return;
    arena_t *a = *pa;

    /* Free all blocks. */
    arena_block_t *blk = a->blocks;
    while (blk) {
        arena_block_t *next = blk->next;
        block_free(alloc, blk);
        blk = next;
    }

    /* Free the arena struct. */
    void *p = (void *)a;
    allocator_free(alloc, &p);
    *pa = NULL;
}

void arena_reset(arena_t *arena) {
    if (!arena) return;
    arena_block_t *blk = arena->blocks;
    while (blk) {
        blk->used = 0;
        blk = blk->next;
    }
    arena->total_used = 0;
}

void *arena_alloc(arena_t *arena, size_t size, size_t align) {
    if (!arena) return NULL;
    if (size == 0) panic("arena: zero-size allocation");

    /* Try each existing block. */
    arena_block_t *blk = arena->blocks;
    while (blk) {
        char *base = block_data(blk) + blk->used;
        size_t aligned_offset = align_up((size_t)base, align) - (size_t)block_data(blk);
        if (aligned_offset >= blk->cap) {
            blk = blk->next;
            continue;
        }
        size_t available = blk->cap - aligned_offset;
        if (available >= size) {
            blk->used = aligned_offset + size;
            arena->total_used += size;
            return block_data(blk) + aligned_offset;
        }
        blk = blk->next;
    }

    /* No existing block fits — allocate a new one. */
    size_t needed = size + align; /* extra padding for alignment */
    if (needed < arena->next_size) needed = arena->next_size;

    arena_block_t *new_blk = block_new(arena->alloc, needed);
    new_blk->next = arena->blocks;
    arena->blocks  = new_blk;
    arena->next_size = needed * 2;

    /* Allocate from the new block. */
    char *base = block_data(new_blk);
    size_t aligned_offset = align_up((size_t)base, align) - (size_t)block_data(new_blk);
    new_blk->used = aligned_offset + size;
    arena->total_used += size;
    return block_data(new_blk) + aligned_offset;
}

void *arena_calloc(arena_t *arena, size_t count, size_t elem_size, size_t align) {
    if (!arena) return NULL;
    if (count == 0 || elem_size == 0) panic("arena: zero-size calloc");

    size_t total = count * elem_size;
    if (total / elem_size != count) panic("arena: calloc overflow");

    void *ptr = arena_alloc(arena, total, align);
    memset(ptr, 0, total);
    return ptr;
}

size_t arena_total_allocated(const arena_t *arena) {
    if (!arena) return 0;
    size_t total = 0;
    const arena_block_t *blk = arena->blocks;
    while (blk) {
        total += blk->cap;
        blk = blk->next;
    }
    return total;
}

size_t arena_total_used(const arena_t *arena) {
    if (!arena) return 0;
    return arena->total_used;
}
