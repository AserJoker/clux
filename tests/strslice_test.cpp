/*
 * Description: strslice_t unit tests
 * Create: 2026-09-07
 */

#include <gtest/gtest.h>

extern "C" {
#include "core/strslice.h"
}

namespace {

/* ---- STRSLICE_EMPTY ---- */

/**
 * Scenario: STRSLICE_EMPTY has NULL pointer and zero length
 * Expected: ptr == NULL, len == 0, is_empty returns true
 */
TEST(StrSlice, EmptyHasNullPtrAndZeroLen) {
    strslice_t s = STRSLICE_EMPTY;
    EXPECT_EQ(s.ptr, nullptr);
    EXPECT_EQ(s.len, 0u);
    EXPECT_TRUE(strslice_is_empty(s));
}

/* ---- STRSLICE_LIT ---- */

/**
 * Scenario: STRSLICE_LIT constructs a slice from a string literal
 * Expected: ptr points to the literal, len equals strlen (without NUL)
 */
TEST(StrSlice, LitFromNonEmpty) {
    strslice_t s = STRSLICE_LIT("hello");
    EXPECT_NE(s.ptr, nullptr);
    EXPECT_EQ(s.len, 5u);
    EXPECT_EQ(memcmp(s.ptr, "hello", 5), 0);
}

/**
 * Scenario: STRSLICE_LIT with an empty string literal
 * Expected: len == 0, is_empty returns true
 */
TEST(StrSlice, LitFromEmptyLiteral) {
    strslice_t s = STRSLICE_LIT("");
    EXPECT_NE(s.ptr, nullptr);
    EXPECT_EQ(s.len, 0u);
    EXPECT_TRUE(strslice_is_empty(s));
}

/* ---- strslice_from_cstr ---- */

/**
 * Scenario: from_cstr with NULL returns empty slice
 * Expected: ptr == NULL, len == 0
 */
TEST(StrSlice, FromCstrNullReturnsEmpty) {
    strslice_t s = strslice_from_cstr(NULL);
    EXPECT_EQ(s.ptr, nullptr);
    EXPECT_EQ(s.len, 0u);
    EXPECT_TRUE(strslice_is_empty(s));
}

/**
 * Scenario: from_cstr with a normal C string
 * Expected: ptr points to the string, len == strlen
 */
TEST(StrSlice, FromCstrNonEmpty) {
    strslice_t s = strslice_from_cstr("world");
    EXPECT_NE(s.ptr, nullptr);
    EXPECT_EQ(s.len, 5u);
    EXPECT_EQ(memcmp(s.ptr, "world", 5), 0);
}

/**
 * Scenario: from_cstr with an empty C string
 * Expected: ptr is non-NULL (points to ""), len == 0
 */
TEST(StrSlice, FromCstrEmptyString) {
    strslice_t s = strslice_from_cstr("");
    EXPECT_NE(s.ptr, nullptr);
    EXPECT_EQ(s.len, 0u);
    EXPECT_TRUE(strslice_is_empty(s));
}

/* ---- strslice_from_bytes ---- */

/**
 * Scenario: from_bytes stores the given pointer and length
 * Expected: ptr and len match the arguments exactly
 */
TEST(StrSlice, FromBytesBasic) {
    const char data[] = "abcdef";
    strslice_t s = strslice_from_bytes(data, 3);
    EXPECT_EQ(s.ptr, data);
    EXPECT_EQ(s.len, 3u);
}

/**
 * Scenario: from_bytes with zero length
 * Expected: ptr is set, len == 0
 */
TEST(StrSlice, FromBytesZeroLen) {
    const char data[] = "x";
    strslice_t s = strslice_from_bytes(data, 0);
    EXPECT_EQ(s.ptr, data);
    EXPECT_EQ(s.len, 0u);
    EXPECT_TRUE(strslice_is_empty(s));
}

/* ---- strslice_eq ---- */

/**
 * Scenario: Two empty slices are equal
 * Expected: returns true
 */
TEST(StrSlice, EqBothEmpty) {
    strslice_t a = STRSLICE_EMPTY;
    strslice_t b = STRSLICE_EMPTY;
    EXPECT_TRUE(strslice_eq(a, b));
}

/**
 * Scenario: Identical non-empty slices are equal
 * Expected: returns true
 */
TEST(StrSlice, EqIdentical) {
    const char *text = "abc";
    strslice_t a = strslice_from_bytes(text, 3);
    strslice_t b = strslice_from_bytes(text, 3);
    EXPECT_TRUE(strslice_eq(a, b));
}

/**
 * Scenario: Different content with same length are not equal
 * Expected: returns false
 */
TEST(StrSlice, EqDifferentContent) {
    strslice_t a = STRSLICE_LIT("abc");
    strslice_t b = STRSLICE_LIT("abd");
    EXPECT_FALSE(strslice_eq(a, b));
}

/**
 * Scenario: Different lengths are not equal
 * Expected: returns false
 */
TEST(StrSlice, EqDifferentLengths) {
    strslice_t a = STRSLICE_LIT("abc");
    strslice_t b = STRSLICE_LIT("ab");
    EXPECT_FALSE(strslice_eq(a, b));
}

/**
 * Scenario: One empty, one non-empty are not equal
 * Expected: returns false
 */
TEST(StrSlice, EqEmptyVsNonEmpty) {
    strslice_t a = STRSLICE_EMPTY;
    strslice_t b = STRSLICE_LIT("x");
    EXPECT_FALSE(strslice_eq(a, b));
    EXPECT_FALSE(strslice_eq(b, a));
}

/* ---- strslice_cmp ---- */

/**
 * Scenario: Lexicographic comparison, a < b (prefix)
 * Expected: returns negative
 */
TEST(StrSlice, CmpLessThan) {
    strslice_t a = STRSLICE_LIT("abc");
    strslice_t b = STRSLICE_LIT("abd");
    EXPECT_LT(strslice_cmp(a, b), 0);
}

/**
 * Scenario: Lexicographic comparison, a > b
 * Expected: returns positive
 */
TEST(StrSlice, CmpGreaterThan) {
    strslice_t a = STRSLICE_LIT("abd");
    strslice_t b = STRSLICE_LIT("abc");
    EXPECT_GT(strslice_cmp(a, b), 0);
}

/**
 * Scenario: Equal slices
 * Expected: returns 0
 */
TEST(StrSlice, CmpEqual) {
    strslice_t a = STRSLICE_LIT("hello");
    strslice_t b = STRSLICE_LIT("hello");
    EXPECT_EQ(strslice_cmp(a, b), 0);
}

/**
 * Scenario: One is a prefix of the other
 * Expected: shorter one compares less
 */
TEST(StrSlice, CmpPrefixIsLess) {
    strslice_t a = STRSLICE_LIT("ab");
    strslice_t b = STRSLICE_LIT("abc");
    EXPECT_LT(strslice_cmp(a, b), 0);
    EXPECT_GT(strslice_cmp(b, a), 0);
}

/**
 * Scenario: Both empty slices compare equal
 * Expected: returns 0
 */
TEST(StrSlice, CmpBothEmpty) {
    strslice_t a = STRSLICE_EMPTY;
    strslice_t b = STRSLICE_EMPTY;
    EXPECT_EQ(strslice_cmp(a, b), 0);
}

/* ---- strslice_hash ---- */

/**
 * Scenario: Same content produces same hash
 * Expected: hash values are equal
 */
TEST(StrSlice, HashConsistentForSameSlice) {
    const char *text = "hashme";
    strslice_t a = strslice_from_bytes(text, 6);
    strslice_t b = strslice_from_bytes(text, 6);
    EXPECT_EQ(strslice_hash(a), strslice_hash(b));
}

/**
 * Scenario: Different content produces different hash (probabilistic)
 * Expected: hash values differ
 */
TEST(StrSlice, HashDiffersForDifferentSlice) {
    strslice_t a = STRSLICE_LIT("foo");
    strslice_t b = STRSLICE_LIT("bar");
    EXPECT_NE(strslice_hash(a), strslice_hash(b));
}

/**
 * Scenario: Empty slice hash is deterministic
 * Expected: calling twice yields same result
 */
TEST(StrSlice, HashEmptyIsDeterministic) {
    strslice_t s = STRSLICE_EMPTY;
    EXPECT_EQ(strslice_hash(s), strslice_hash(s));
}

/* ---- strslice_substr ---- */

/**
 * Scenario: Normal substring extraction
 * Expected: correct ptr offset and length
 */
TEST(StrSlice, SubstrNormal) {
    strslice_t s = STRSLICE_LIT("hello world");
    strslice_t sub = strslice_substr(s, 6, 5);
    EXPECT_EQ(sub.len, 5u);
    EXPECT_EQ(memcmp(sub.ptr, "world", 5), 0);
}

/**
 * Scenario: Substring with length exceeding available bytes is clamped
 * Expected: length clamped to available bytes from start
 */
TEST(StrSlice, SubstrLengthClamped) {
    strslice_t s = STRSLICE_LIT("abc");
    strslice_t sub = strslice_substr(s, 1, 10);
    EXPECT_EQ(sub.len, 2u);
    EXPECT_EQ(memcmp(sub.ptr, "bc", 2), 0);
}

/**
 * Scenario: Start index beyond end returns empty
 * Expected: returns STRSLICE_EMPTY
 */
TEST(StrSlice, SubstrStartOutOfBounds) {
    strslice_t s = STRSLICE_LIT("abc");
    strslice_t sub = strslice_substr(s, 5, 1);
    EXPECT_EQ(sub.ptr, nullptr);
    EXPECT_EQ(sub.len, 0u);
    EXPECT_TRUE(strslice_is_empty(sub));
}

/**
 * Scenario: Substring from empty slice returns empty
 * Expected: returns STRSLICE_EMPTY
 */
TEST(StrSlice, SubstrFromEmptySlice) {
    strslice_t s = STRSLICE_EMPTY;
    strslice_t sub = strslice_substr(s, 0, 1);
    EXPECT_EQ(sub.ptr, nullptr);
    EXPECT_EQ(sub.len, 0u);
    EXPECT_TRUE(strslice_is_empty(sub));
}

/* ---- strslice_starts_with ---- */

/**
 * Scenario: Slice starts with given prefix
 * Expected: returns true
 */
TEST(StrSlice, StartsWithTrue) {
    strslice_t s = STRSLICE_LIT("hello world");
    strslice_t prefix = STRSLICE_LIT("hello");
    EXPECT_TRUE(strslice_starts_with(s, prefix));
}

/**
 * Scenario: Slice does not start with given prefix
 * Expected: returns false
 */
TEST(StrSlice, StartsWithFalse) {
    strslice_t s = STRSLICE_LIT("hello world");
    strslice_t prefix = STRSLICE_LIT("world");
    EXPECT_FALSE(strslice_starts_with(s, prefix));
}

/**
 * Scenario: Empty prefix always matches
 * Expected: returns true
 */
TEST(StrSlice, StartsWithEmptyPrefix) {
    strslice_t s = STRSLICE_LIT("hello");
    strslice_t prefix = STRSLICE_EMPTY;
    EXPECT_TRUE(strslice_starts_with(s, prefix));
}

/**
 * Scenario: Prefix longer than slice returns false
 * Expected: returns false
 */
TEST(StrSlice, StartsWithPrefixLongerThanSlice) {
    strslice_t s = STRSLICE_LIT("hi");
    strslice_t prefix = STRSLICE_LIT("hello");
    EXPECT_FALSE(strslice_starts_with(s, prefix));
}

/**
 * Scenario: Empty slice with empty prefix returns true
 * Expected: returns true
 */
TEST(StrSlice, StartsWithBothEmpty) {
    strslice_t s = STRSLICE_EMPTY;
    strslice_t prefix = STRSLICE_EMPTY;
    EXPECT_TRUE(strslice_starts_with(s, prefix));
}

/* ---- strslice_ends_with ---- */

/**
 * Scenario: Slice ends with given suffix
 * Expected: returns true
 */
TEST(StrSlice, EndsWithTrue) {
    strslice_t s = STRSLICE_LIT("hello world");
    strslice_t suffix = STRSLICE_LIT("world");
    EXPECT_TRUE(strslice_ends_with(s, suffix));
}

/**
 * Scenario: Slice does not end with given suffix
 * Expected: returns false
 */
TEST(StrSlice, EndsWithFalse) {
    strslice_t s = STRSLICE_LIT("hello world");
    strslice_t suffix = STRSLICE_LIT("hello");
    EXPECT_FALSE(strslice_ends_with(s, suffix));
}

/**
 * Scenario: Empty suffix always matches
 * Expected: returns true
 */
TEST(StrSlice, EndsWithEmptySuffix) {
    strslice_t s = STRSLICE_LIT("hello");
    strslice_t suffix = STRSLICE_EMPTY;
    EXPECT_TRUE(strslice_ends_with(s, suffix));
}

/**
 * Scenario: Suffix longer than slice returns false
 * Expected: returns false
 */
TEST(StrSlice, EndsWithSuffixLongerThanSlice) {
    strslice_t s = STRSLICE_LIT("hi");
    strslice_t suffix = STRSLICE_LIT("hello");
    EXPECT_FALSE(strslice_ends_with(s, suffix));
}

/**
 * Scenario: Empty slice with empty suffix returns true
 * Expected: returns true
 */
TEST(StrSlice, EndsWithBothEmpty) {
    strslice_t s = STRSLICE_EMPTY;
    strslice_t suffix = STRSLICE_EMPTY;
    EXPECT_TRUE(strslice_ends_with(s, suffix));
}

} // namespace
