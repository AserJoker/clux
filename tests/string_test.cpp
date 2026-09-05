#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "core/allocator.h"
#include "core/panic.h"
#include "core/string.h"
}

/* ---- Test allocator helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr) { free(ptr); }

/* ---- Panic handler for death / exception tests ---- */

static thread_local std::string g_last_string_panic;

extern "C" void string_throw_handler(const char *message) {
  g_last_string_panic = message;
  throw std::runtime_error(message);
}

class StringPanicTest : public ::testing::Test {
protected:
  panic_handler_t saved_;
  void SetUp() override { saved_ = get_panic_handler(); }
  void TearDown() override { set_panic_handler(saved_); }
};

/* ==== Construction ==== */

TEST(StringNew, EmptyString) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(string_len(s), 0u);
  EXPECT_EQ(string_cap(s), 0u);
  EXPECT_TRUE(string_is_empty(s));
  EXPECT_STREQ(string_cstr(s), "");
  string_free(&s);
  EXPECT_EQ(s, nullptr);
  delete_allocator(&a);
}

TEST(StringNew, NullAllocator) { EXPECT_EQ(string_new(NULL), nullptr); }

TEST(StringFromCstr, Basic) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "hello");
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(string_len(s), 5u);
  EXPECT_STREQ(string_cstr(s), "hello");
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringFromCstr, Empty) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "");
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(string_len(s), 0u);
  EXPECT_TRUE(string_is_empty(s));
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringFromCstr, NullInput) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(string_from_cstr(a, nullptr), nullptr);
  EXPECT_EQ(string_from_cstr(NULL, "x"), nullptr);
  delete_allocator(&a);
}

TEST(StringFromBytes, RawBytes) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  const char data[] = {'a', '\0', 'b', 'c'};
  string_t *s = string_from_bytes(a, data, sizeof(data));
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(string_len(s), 4u); /* embedded NUL preserved */
  EXPECT_EQ(string_char_at(s, 1), 0);
  EXPECT_EQ(string_char_at(s, 2), 'b');
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringFromString, Copy) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s1 = string_from_cstr(a, "copy me");
  string_t *s2 = string_from_string(a, s1);
  ASSERT_NE(s2, nullptr);
  EXPECT_NE(s2, s1);
  EXPECT_STREQ(string_cstr(s2), "copy me");
  string_free(&s1);
  string_free(&s2);
  delete_allocator(&a);
}

/* ==== Accessors ==== */

TEST(StringAccess, CharAt) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "abc");
  EXPECT_EQ(string_char_at(s, 0), 'a');
  EXPECT_EQ(string_char_at(s, 2), 'c');
  EXPECT_EQ(string_char_at(s, 3), -1); /* out of bounds */
  EXPECT_EQ(string_char_at(s, 99), -1);
  EXPECT_EQ(string_char_at(nullptr, 0), -1);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringAccess, NullSafe) {
  EXPECT_EQ(string_len(nullptr), 0u);
  EXPECT_EQ(string_cap(nullptr), 0u);
  EXPECT_TRUE(string_is_empty(nullptr));
  EXPECT_STREQ(string_cstr(nullptr), "");
  EXPECT_STREQ(string_data(nullptr), "");
  EXPECT_EQ(string_char_at(nullptr, 0), -1);
}

/* ==== Append ==== */

TEST(StringAppend, Cstr) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  string_append_cstr(s, "foo");
  string_append_cstr(s, "bar");
  EXPECT_EQ(string_len(s), 6u);
  EXPECT_STREQ(string_cstr(s), "foobar");
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringAppend, BytesAndChar) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "ab");
  string_append_bytes(s, "cd", 2);
  string_append_char(s, 'e');
  EXPECT_STREQ(string_cstr(s), "abcde");
  EXPECT_EQ(string_len(s), 5u);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringAppend, String) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s1 = string_from_cstr(a, "hello ");
  string_t *s2 = string_from_cstr(a, "world");
  string_append_string(s1, s2);
  EXPECT_STREQ(string_cstr(s1), "hello world");
  string_free(&s1);
  string_free(&s2);
  delete_allocator(&a);
}

TEST(StringAppend, NullSafe) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "x");
  string_append_cstr(s, nullptr); /* no-op */
  string_append_bytes(s, nullptr, 3);
  string_append_bytes(s, "y", 0);
  string_append_char(nullptr, 'z');
  EXPECT_STREQ(string_cstr(s), "x");
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringAppend, AutoGrowth) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  size_t prev_cap = 0;
  for (int i = 0; i < 100; i++) {
    string_append_char(s, 'x');
    if (string_cap(s) > prev_cap) {
      if (prev_cap > 0)
        EXPECT_GE(string_cap(s), prev_cap * 2);
      prev_cap = string_cap(s);
    }
  }
  EXPECT_EQ(string_len(s), 100u);
  for (int i = 0; i < 100; i++)
    EXPECT_EQ(string_char_at(s, (size_t)i), 'x');
  string_free(&s);
  delete_allocator(&a);
}

/* ==== Assign / Clear / Reserve / Shrink ==== */

TEST(StringMutate, Assign) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "old content");
  string_assign_cstr(s, "new");
  EXPECT_STREQ(string_cstr(s), "new");
  EXPECT_EQ(string_len(s), 3u);
  string_assign_bytes(s, "xy", 2);
  EXPECT_STREQ(string_cstr(s), "xy");
  string_assign_cstr(s, nullptr); /* no-op */
  EXPECT_STREQ(string_cstr(s), "xy");
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringMutate, Clear) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "something");
  size_t cap_before = string_cap(s);
  string_clear(s);
  EXPECT_TRUE(string_is_empty(s));
  EXPECT_STREQ(string_cstr(s), "");
  EXPECT_EQ(string_cap(s), cap_before); /* capacity retained */
  string_append_cstr(s, "again");
  EXPECT_STREQ(string_cstr(s), "again");
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringMutate, Reserve) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  string_reserve(s, 64);
  EXPECT_GE(string_cap(s), 64u);
  EXPECT_EQ(string_len(s), 0u);
  size_t cap_before = string_cap(s);
  string_reserve(s, 10); /* smaller: no-op */
  EXPECT_EQ(string_cap(s), cap_before);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringMutate, ShrinkToFit) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  string_reserve(s, 128);
  string_append_cstr(s, "small");
  EXPECT_GE(string_cap(s), 128u);
  string_shrink_to_fit(s);
  EXPECT_EQ(string_cap(s), 6u); /* 5 bytes + NUL */
  EXPECT_STREQ(string_cstr(s), "small");
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringMutate, ShrinkToFitEmpty) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  string_reserve(s, 32);
  string_shrink_to_fit(s);
  EXPECT_EQ(string_cap(s), 0u);
  EXPECT_STREQ(string_cstr(s), "");
  string_free(&s);
  delete_allocator(&a);
}

/* ==== Searching ==== */

TEST(StringSearch, Find) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "hello world, hello clux");
  EXPECT_EQ(string_find(s, "hello", 0), 0u);
  EXPECT_EQ(string_find(s, "hello", 1), 13u);
  EXPECT_EQ(string_find(s, "clux", 0), 19u);
  EXPECT_EQ(string_find(s, "zzz", 0), STRING_NPOS);
  EXPECT_EQ(string_find(s, "", 5), 5u);  /* empty needle matches at start */
  EXPECT_EQ(string_find(s, "x", 100), STRING_NPOS);
  EXPECT_EQ(string_find(s, "hello", 100), STRING_NPOS);
  EXPECT_EQ(string_find(nullptr, "x", 0), STRING_NPOS);
  EXPECT_EQ(string_find(s, nullptr, 0), STRING_NPOS);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringSearch, RFind) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "ababa");
  EXPECT_EQ(string_rfind(s, "aba"), 2u);
  EXPECT_EQ(string_rfind(s, "ab"), 2u);
  EXPECT_EQ(string_rfind(s, "ba"), 3u);
  EXPECT_EQ(string_rfind(s, "zz"), STRING_NPOS);
  EXPECT_EQ(string_rfind(s, ""), 5u);
  EXPECT_EQ(string_rfind(s, "ababa"), 0u);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringSearch, Contains) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "needle in a haystack");
  EXPECT_TRUE(string_contains(s, "hay"));
  EXPECT_TRUE(string_contains(s, ""));
  EXPECT_FALSE(string_contains(s, "needlezz"));
  EXPECT_FALSE(string_contains(s, nullptr));
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringSearch, StartsEndsWith) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "prefix-middle-suffix");
  EXPECT_TRUE(string_starts_with(s, "prefix"));
  EXPECT_TRUE(string_starts_with(s, ""));
  EXPECT_FALSE(string_starts_with(s, "prefiz"));
  EXPECT_TRUE(string_ends_with(s, "suffix"));
  EXPECT_TRUE(string_ends_with(s, ""));
  EXPECT_FALSE(string_ends_with(s, "suffi"));
  EXPECT_FALSE(string_ends_with(s, "suffixx"));
  EXPECT_FALSE(string_starts_with(s, nullptr));
  EXPECT_FALSE(string_ends_with(s, nullptr));
  string_free(&s);
  delete_allocator(&a);
}

/* ==== Comparison ==== */

TEST(StringCompare, Ordering) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s1 = string_from_cstr(a, "abc");
  string_t *s2 = string_from_cstr(a, "abc");
  string_t *s3 = string_from_cstr(a, "abd");
  string_t *s4 = string_from_cstr(a, "ab");
  string_t *s5 = string_from_cstr(a, "abcde");

  EXPECT_EQ(string_compare(s1, s2), 0);
  EXPECT_LT(string_compare(s1, s3), 0);
  EXPECT_GT(string_compare(s3, s1), 0);
  EXPECT_GT(string_compare(s1, s4), 0); /* longer string is greater */
  EXPECT_LT(string_compare(s4, s1), 0);
  EXPECT_LT(string_compare(s1, s5), 0);
  EXPECT_EQ(string_compare(nullptr, nullptr), 0);
  EXPECT_LT(string_compare(nullptr, s1), 0);
  EXPECT_GT(string_compare(s1, nullptr), 0);

  EXPECT_TRUE(string_equals(s1, s2));
  EXPECT_FALSE(string_equals(s1, s3));
  EXPECT_FALSE(string_equals(s1, nullptr));
  EXPECT_TRUE(string_equals(nullptr, nullptr));

  string_free(&s1);
  string_free(&s2);
  string_free(&s3);
  string_free(&s4);
  string_free(&s5);
  delete_allocator(&a);
}

/* ==== Derived strings ==== */

TEST(StringDerived, Substring) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "hello world");
  string_t *sub = string_substring(a, s, 6, 5);
  ASSERT_NE(sub, nullptr);
  EXPECT_STREQ(string_cstr(sub), "world");

  /* len clamped to available bytes */
  string_t *sub2 = string_substring(a, s, 0, 100);
  ASSERT_NE(sub2, nullptr);
  EXPECT_STREQ(string_cstr(sub2), "hello world");

  /* empty range */
  string_t *sub3 = string_substring(a, s, 4, 0);
  ASSERT_NE(sub3, nullptr);
  EXPECT_TRUE(string_is_empty(sub3));

  /* out of bounds start */
  EXPECT_EQ(string_substring(a, s, 100, 1), nullptr);
  EXPECT_EQ(string_substring(a, nullptr, 0, 1), nullptr);

  string_free(&sub);
  string_free(&sub2);
  string_free(&sub3);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringDerived, Concat) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s1 = string_from_cstr(a, "foo");
  string_t *s2 = string_from_cstr(a, "bar");
  string_t *s3 = string_from_cstr(a, "");
  string_t *c = string_concat(a, s1, s2);
  ASSERT_NE(c, nullptr);
  EXPECT_STREQ(string_cstr(c), "foobar");

  string_t *c2 = string_concat(a, s1, s3);
  EXPECT_STREQ(string_cstr(c2), "foo");

  EXPECT_EQ(string_concat(a, nullptr, s2), nullptr);
  EXPECT_EQ(string_concat(a, s1, nullptr), nullptr);

  /* originals untouched */
  EXPECT_STREQ(string_cstr(s1), "foo");
  EXPECT_STREQ(string_cstr(s2), "bar");

  string_free(&c);
  string_free(&c2);
  string_free(&s1);
  string_free(&s2);
  string_free(&s3);
  delete_allocator(&a);
}

TEST(StringDerived, ReplaceFirst) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "one two one two");
  string_t *r = string_replace(a, s, "one", "1");
  ASSERT_NE(r, nullptr);
  EXPECT_STREQ(string_cstr(r), "1 two one two");

  /* replacement NULL deletes the match */
  string_t *r2 = string_replace(a, s, " two", nullptr);
  EXPECT_STREQ(string_cstr(r2), "one one two");

  /* not found -> copy */
  string_t *r3 = string_replace(a, s, "zzz", "x");
  EXPECT_NE(r3, s);
  EXPECT_STREQ(string_cstr(r3), "one two one two");

  /* empty needle -> copy */
  string_t *r4 = string_replace(a, s, "", "x");
  EXPECT_STREQ(string_cstr(r4), "one two one two");

  EXPECT_EQ(string_replace(a, nullptr, "x", "y"), nullptr);
  EXPECT_EQ(string_replace(a, s, nullptr, "y"), nullptr);

  /* original untouched */
  EXPECT_STREQ(string_cstr(s), "one two one two");

  string_free(&r);
  string_free(&r2);
  string_free(&r3);
  string_free(&r4);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringDerived, ReplaceAll) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "one two one two one");

  string_t *r = string_replace_all(a, s, "one", "1");
  ASSERT_NE(r, nullptr);
  EXPECT_STREQ(string_cstr(r), "1 two 1 two 1");

  /* delete all matches */
  string_t *r2 = string_replace_all(a, s, "one ", nullptr);
  EXPECT_STREQ(string_cstr(r2), "two two one");

  /* adjacent matches */
  string_t *s2 = string_from_cstr(a, "aaaa");
  string_t *r3 = string_replace_all(a, s2, "aa", "b");
  EXPECT_STREQ(string_cstr(r3), "bb");
  string_free(&r3);

  /* replacement longer than needle */
  string_t *r4 = string_replace_all(a, s2, "a", "xy");
  EXPECT_STREQ(string_cstr(r4), "xyxyxyxy");
  string_free(&r4);

  /* no matches -> copy */
  string_t *r5 = string_replace_all(a, s2, "z", "q");
  EXPECT_NE(r5, s2);
  EXPECT_STREQ(string_cstr(r5), "aaaa");
  string_free(&r5);

  /* originals untouched */
  EXPECT_STREQ(string_cstr(s), "one two one two one");
  EXPECT_STREQ(string_cstr(s2), "aaaa");

  string_free(&r);
  string_free(&r2);
  string_free(&s);
  string_free(&s2);
  delete_allocator(&a);
}

/* ==== allocator_move / allocator_clone ==== */

TEST(StringMove, TransferOwnership) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "movable");
  string_t *moved = (string_t *)allocator_move(a, (void **)&s);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(s, nullptr); /* source nullified */
  EXPECT_STREQ(string_cstr(moved), "movable");
  EXPECT_EQ(string_len(moved), 7u);
  string_free(&moved);
  delete_allocator(&a);
}

TEST(StringClone, DeepCopy) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "clone me");
  void *s_ptr = s;
  string_t *cloned = (string_t *)allocator_clone(a, (void **)&s_ptr);
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(s, s_ptr); /* source unchanged */
  EXPECT_STREQ(string_cstr(cloned), "clone me");
  EXPECT_STREQ(string_cstr(s), "clone me");

  /* independent buffers: mutating one must not affect the other */
  string_append_cstr(cloned, "!");
  EXPECT_STREQ(string_cstr(cloned), "clone me!");
  EXPECT_STREQ(string_cstr(s), "clone me");

  string_free(&cloned);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringClone, EmptyClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  void *s_ptr = s;
  string_t *cloned = (string_t *)allocator_clone(a, (void **)&s_ptr);
  ASSERT_NE(cloned, nullptr);
  EXPECT_TRUE(string_is_empty(cloned));
  string_free(&cloned);
  string_free(&s);
  delete_allocator(&a);
}

TEST(StringMove, MovedFromSafeDispose) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_from_cstr(a, "data");
  /* allocator_move: move_fn then allocator_free on source; the source's
   * dispose_fn must see data=NULL and skip freeing the buffer. */
  string_t *moved = (string_t *)allocator_move(a, (void **)&s);
  EXPECT_EQ(s, nullptr);
  EXPECT_STREQ(string_cstr(moved), "data");
  string_free(&moved);
  delete_allocator(&a);
}

/* ==== Panic tests ==== */

TEST_F(StringPanicTest, ReserveOverflowPanics) {
  set_panic_handler(string_throw_handler);
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  try {
    string_reserve(s, SIZE_MAX); /* len + SIZE_MAX + 1 overflows */
    FAIL() << "should have panicked";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("capacity overflow"),
              std::string::npos);
  }
  string_free(&s);
  delete_allocator(&a);
}

TEST_F(StringPanicTest, AppendOverflowPanics) {
  set_panic_handler(string_throw_handler);
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_t *s = string_new(a);
  try {
    string_append_bytes(s, "x", SIZE_MAX); /* len + SIZE_MAX + 1 overflows */
    FAIL() << "should have panicked";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("capacity overflow"),
              std::string::npos);
  }
  string_free(&s);
  delete_allocator(&a);
}

/* ==== NULL safety ==== */

TEST(StringNullSafe, FreeAndMutate) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  string_free(nullptr); /* no-op */
  string_t *null_str = nullptr;
  string_free(&null_str); /* no-op */
  EXPECT_EQ(null_str, nullptr);
  string_clear(nullptr);
  string_reserve(nullptr, 10);
  string_shrink_to_fit(nullptr);
  string_append_cstr(nullptr, "x");
  string_append_string(nullptr, nullptr);
  string_assign_cstr(nullptr, "x");
  delete_allocator(&a);
}
