/*
 * Description: arena_t unit tests
 * Create: 2026-09-07
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

extern "C" {
#include "core/allocator.h"
#include "core/arena.h"
#include "core/panic.h"
}

#include "test_common.h"

namespace {

/* ---- Panic-throwing handler for EXPECT_DEATH tests ---- */

static void panic_throw(const char *message) {
    (void)message;
    abort();
}

class ArenaTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_handler_ = get_panic_handler();
        set_panic_handler(panic_throw);
        alloc_ = create_allocator(malloc, free);
    }

    void TearDown() override {
        set_panic_handler(saved_handler_);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc_);
    }

    allocator_t *alloc_ = nullptr;

private:
    panic_handler_t saved_handler_;
};

/* ---- arena_new / arena_destroy ---- */

/**
 * Scenario: Create and destroy an arena normally
 * Expected: arena is non-NULL, destroy nullifies the pointer,
 *           allocator has no live allocations after teardown
 */
TEST_F(ArenaTest, NewDestroyBasic) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);
    EXPECT_GT(arena_total_allocated(a), 0u);

    arena_destroy(alloc_, &a);
    EXPECT_EQ(a, nullptr);
}

/**
 * Scenario: arena_new_default creates arena with default block size
 * Expected: arena is non-NULL, total allocated >= ARENA_DEFAULT_BLOCK_SIZE
 */
TEST_F(ArenaTest, NewDefault) {
    arena_t *a = arena_new_default(alloc_);
    ASSERT_NE(a, nullptr);
    EXPECT_GE(arena_total_allocated(a), ARENA_DEFAULT_BLOCK_SIZE);

    arena_destroy(alloc_, &a);
    EXPECT_EQ(a, nullptr);
}

/**
 * Scenario: arena_new with NULL allocator returns NULL
 * Expected: returns NULL, no crash
 */
TEST_F(ArenaTest, NewNullAllocReturnsNull) {
    arena_t *a = arena_new(nullptr, 256);
    EXPECT_EQ(a, nullptr);
}

/**
 * Scenario: arena_new with block_size == 0 returns NULL
 * Expected: returns NULL, no crash
 */
TEST_F(ArenaTest, NewZeroSizeReturnsNull) {
    arena_t *a = arena_new(alloc_, 0);
    EXPECT_EQ(a, nullptr);
}

/**
 * Scenario: arena_destroy with NULL pointer is safe
 * Expected: no crash, no-op
 */
TEST_F(ArenaTest, DestroyNullPointerSafe) {
    arena_t *a = nullptr;
    arena_destroy(alloc_, &a);
    EXPECT_EQ(a, nullptr);

    // Also safe with NULL double-pointer
    arena_destroy(alloc_, nullptr);
}

/* ---- arena_alloc ---- */

/**
 * Scenario: Allocate memory from arena, write and read back
 * Expected: allocated memory is usable (writable and readable)
 */
TEST_F(ArenaTest, AllocBasic) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);

    void *ptr = arena_alloc(a, 64, 1);
    ASSERT_NE(ptr, nullptr);

    // Write a pattern and read it back
    std::memset(ptr, 0xAB, 64);
    auto *bytes = static_cast<unsigned char *>(ptr);
    for (size_t i = 0; i < 64; i++) {
        EXPECT_EQ(bytes[i], 0xAB);
    }

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: Allocate with various alignments
 * Expected: returned pointer is properly aligned
 */
TEST_F(ArenaTest, AllocAlignment) {
    arena_t *a = arena_new(alloc_, 4096);
    ASSERT_NE(a, nullptr);

    size_t alignments[] = {1, 2, 4, 8, 16, 32, 64};
    for (size_t align : alignments) {
        void *ptr = arena_alloc(a, 32, align);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % align, 0u)
            << "alignment " << align << " failed";
    }

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: Multiple allocations that exceed the initial block size
 *          trigger creation of new blocks
 * Expected: total_allocated increases as new blocks are added
 */
TEST_F(ArenaTest, AllocMultipleBlocks) {
    arena_t *a = arena_new(alloc_, 64);
    ASSERT_NE(a, nullptr);

    size_t initial = arena_total_allocated(a);

    // Allocate enough to overflow the first block
    for (int i = 0; i < 20; i++) {
        void *ptr = arena_alloc(a, 32, 1);
        ASSERT_NE(ptr, nullptr);
        std::memset(ptr, static_cast<unsigned char>(i), 32);
    }

    size_t after = arena_total_allocated(a);
    EXPECT_GT(after, initial);

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_alloc with NULL arena returns NULL
 * Expected: returns NULL, no crash
 */
TEST_F(ArenaTest, AllocNullArenaReturnsNull) {
    void *ptr = arena_alloc(nullptr, 16, 1);
    EXPECT_EQ(ptr, nullptr);
}

/**
 * Scenario: arena_alloc with size == 0 panics
 * Expected: process terminates (panic)
 */
TEST_F(ArenaTest, AllocZeroSizePanics) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);

    EXPECT_DEATH(arena_alloc(a, 0, 1), ".*");

    arena_destroy(alloc_, &a);
}

/* ---- arena_calloc ---- */

/**
 * Scenario: arena_calloc returns zero-initialized memory
 * Expected: all bytes are zero
 */
TEST_F(ArenaTest, CallocZeroInitialized) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);

    void *ptr = arena_calloc(a, 10, 8, 8);
    ASSERT_NE(ptr, nullptr);

    auto *bytes = static_cast<unsigned char *>(ptr);
    for (size_t i = 0; i < 80; i++) {
        EXPECT_EQ(bytes[i], 0) << "byte " << i << " not zero";
    }

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_calloc with NULL arena returns NULL
 * Expected: returns NULL, no crash
 */
TEST_F(ArenaTest, CallocNullArenaReturnsNull) {
    void *ptr = arena_calloc(nullptr, 4, 8, 1);
    EXPECT_EQ(ptr, nullptr);
}

/**
 * Scenario: arena_calloc with count == 0 panics
 * Expected: process terminates
 */
TEST_F(ArenaTest, CallocZeroCountPanics) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);

    EXPECT_DEATH(arena_calloc(a, 0, 8, 1), ".*");

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_calloc with elem_size == 0 panics
 * Expected: process terminates
 */
TEST_F(ArenaTest, CallocZeroElemSizePanics) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);

    EXPECT_DEATH(arena_calloc(a, 4, 0, 1), ".*");

    arena_destroy(alloc_, &a);
}

/* ---- arena_reset ---- */

/**
 * Scenario: After arena_reset, total_used is 0 but blocks are retained
 * Expected: total_used resets, total_allocated stays the same,
 *           subsequent allocations reuse existing blocks
 */
TEST_F(ArenaTest, ResetReusesBlocks) {
    arena_t *a = arena_new(alloc_, 256);
    ASSERT_NE(a, nullptr);

    void *ptr1 = arena_alloc(a, 64, 1);
    ASSERT_NE(ptr1, nullptr);
    EXPECT_GT(arena_total_used(a), 0u);

    size_t allocated_before = arena_total_allocated(a);

    arena_reset(a);
    EXPECT_EQ(arena_total_used(a), 0u);
    EXPECT_EQ(arena_total_allocated(a), allocated_before);

    // Allocate again — should reuse existing blocks
    void *ptr2 = arena_alloc(a, 64, 1);
    ASSERT_NE(ptr2, nullptr);
    std::memset(ptr2, 0xCC, 64);

    size_t allocated_after = arena_total_allocated(a);
    EXPECT_EQ(allocated_after, allocated_before);

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_reset with NULL arena is a safe no-op
 * Expected: no crash, no panic handler invoked, global state unchanged
 */
TEST_F(ArenaTest, ResetNullArenaNoThrow) {
    panic_handler_t before = get_panic_handler();
    arena_reset(nullptr);
    EXPECT_EQ(get_panic_handler(), before);
}

/* ---- arena_total_used ---- */

/**
 * Scenario: total_used tracks cumulative allocation size
 * Expected: increases with each allocation
 */
TEST_F(ArenaTest, TotalUsedTracksAllocations) {
    arena_t *a = arena_new(alloc_, 4096);
    ASSERT_NE(a, nullptr);

    EXPECT_EQ(arena_total_used(a), 0u);

    arena_alloc(a, 100, 1);
    EXPECT_GE(arena_total_used(a), 100u);

    arena_alloc(a, 200, 1);
    EXPECT_GE(arena_total_used(a), 300u);

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_total_used with NULL arena returns 0
 * Expected: returns 0
 */
TEST_F(ArenaTest, TotalUsedNullArenaReturnsZero) {
    EXPECT_EQ(arena_total_used(nullptr), 0u);
}

/**
 * Scenario: arena_total_allocated with NULL arena returns 0
 * Expected: returns 0
 */
TEST_F(ArenaTest, TotalAllocatedNullArenaReturnsZero) {
    EXPECT_EQ(arena_total_allocated(nullptr), 0u);
}

/**
 * Scenario: High alignment on a nearly-full block causes the allocator
 *          to skip that block and allocate from a new one.
 * Expected: allocation succeeds despite block skip
 */
TEST_F(ArenaTest, AllocSkipsBlockOnAlignmentOverflow) {
    arena_t *a = arena_new(alloc_, 64);
    ASSERT_NE(a, nullptr);

    // Fill most of the first block
    void *p1 = arena_alloc(a, 48, 1);
    ASSERT_NE(p1, nullptr);

    // Request 16 bytes with 64-byte alignment
    void *p2 = arena_alloc(a, 16, 64);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0u);

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_calloc with count*elem_size overflow panics
 * Expected: process terminates (panic)
 */
TEST_F(ArenaTest, CallocOverflowPanics) {
    arena_t *a = arena_new(alloc_, 4096);
    ASSERT_NE(a, nullptr);

    EXPECT_DEATH(arena_calloc(a, SIZE_MAX, 2, 1), ".*");

    arena_destroy(alloc_, &a);
}

/**
 * Scenario: arena_reset with multiple blocks resets all blocks' used counters
 * Expected: after reset, total_used is 0 and all blocks have used==0,
 *           verified by allocating again from the first block
 */
TEST_F(ArenaTest, ResetMultipleBlocks) {
    arena_t *a = arena_new(alloc_, 64);
    ASSERT_NE(a, nullptr);

    // Force creation of multiple blocks
    for (int i = 0; i < 5; i++) {
        void *ptr = arena_alloc(a, 32, 1);
        ASSERT_NE(ptr, nullptr);
        std::memset(ptr, static_cast<unsigned char>(i), 32);
    }

    EXPECT_GT(arena_total_used(a), 0u);
    size_t alloc_before = arena_total_allocated(a);

    arena_reset(a);
    EXPECT_EQ(arena_total_used(a), 0u);
    EXPECT_EQ(arena_total_allocated(a), alloc_before);

    // Verify reuse after reset
    void *p = arena_alloc(a, 16, 1);
    ASSERT_NE(p, nullptr);
    std::memset(p, 0xFF, 16);

    arena_destroy(alloc_, &a);
}

} // namespace
