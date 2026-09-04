#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <random>

extern "C" {
#include "core/allocator.h"
#include "core/panic.h"
#include "core/rbtree.h"
}

/* ---- Test allocator helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr) { free(ptr); }

/* ---- Comparison functions ---- */

static int cmp_int(const void *a, const void *b) {
  int ia = *(const int *)a;
  int ib = *(const int *)b;
  return (ia > ib) - (ia < ib);
}

/* ---- Simple int class for owned-element tests ---- */

typedef struct { int value; } int_box_t;

static class_t int_box_class = {
    .name = "int_box",
    .size = sizeof(int_box_t),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

static int cmp_int_box(const void *a, const void *b) {
  int ia = ((const int_box_t *)a)->value;
  int ib = ((const int_box_t *)b)->value;
  return (ia > ib) - (ia < ib);
}

/* ---- String box for deep-clone tests ---- */

static class_t byte_class = {
    .name = "byte",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

typedef struct { char *str; } str_box_t;

static void str_box_clone(void *self, allocator_t *allocator, void *another) {
  str_box_t *dst = (str_box_t *)self;
  str_box_t *src = (str_box_t *)another;
  size_t len = strlen(src->str) + 1;
  dst->str = (char *)allocator_new(allocator, &byte_class, len);
  memcpy(dst->str, src->str, len);
}

static void str_box_dispose(void *self, allocator_t *allocator) {
  str_box_t *obj = (str_box_t *)self;
  if (obj->str) {
    void *p = obj->str;
    allocator_free(allocator, &p);
    obj->str = NULL;
  }
}

static void str_box_move(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  str_box_t *dst = (str_box_t *)self;
  str_box_t *src = (str_box_t *)another;
  dst->str = src->str;
  src->str = NULL;
}

static class_t str_box_class = {
    .name = "str_box",
    .size = sizeof(str_box_t),
    .move_fn = str_box_move,
    .clone_fn = str_box_clone,
    .dispose_fn = str_box_dispose,
};

static int cmp_str_box(const void *a, const void *b) {
  return strcmp(((const str_box_t *)a)->str, ((const str_box_t *)b)->str);
}

/* ---- Panic handler ---- */

static thread_local std::string g_last_rb_panic;

extern "C" void rb_throw_handler(const char *message) {
  g_last_rb_panic = message;
  throw std::runtime_error(message);
}

class RbTreePanicTest : public ::testing::Test {
protected:
  panic_handler_t saved_;
  void SetUp() override { saved_ = get_panic_handler(); }
  void TearDown() override { set_panic_handler(saved_); }
};

/* ==== RbTreeNew / RbTreeFree ==== */

TEST(RbTreeNew, EmptyTree) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(rbtree_size(t), 0u);
  EXPECT_TRUE(rbtree_is_empty(t));
  EXPECT_FALSE(rbtree_owns_element(t));
  EXPECT_EQ(rbtree_min(t), nullptr);
  EXPECT_EQ(rbtree_max(t), nullptr);
  rbtree_free(a, &t);
  EXPECT_EQ(t, nullptr);
  delete_allocator(&a);
}

TEST(RbTreeNew, OwnedTree) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int_box, true);
  EXPECT_TRUE(rbtree_owns_element(t));
  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeNew, NullArgs) {
  EXPECT_EQ(rbtree_new(NULL, cmp_int, false), nullptr);
  EXPECT_EQ(rbtree_new(create_allocator(test_alloc, test_free), NULL, false),
            nullptr);
}

TEST(RbTreeFree, NullSafe) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_free(a, nullptr);
  rbtree_t *null_t = nullptr;
  rbtree_free(a, &null_t);
  rbtree_free(nullptr, &null_t);
  delete_allocator(&a);
}

/* ==== Insert / Find / Contains ==== */

TEST(RbTreeInsert, SingleInsert) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int val = 42;
  void *result = rbtree_insert(t, a, &val);
  EXPECT_EQ(result, nullptr); /* no duplicate */
  EXPECT_EQ(rbtree_size(t), 1u);
  EXPECT_FALSE(rbtree_is_empty(t));

  int key = 42;
  void *found = rbtree_find(t, &key);
  EXPECT_EQ(found, &val);
  EXPECT_TRUE(rbtree_contains(t, &key));

  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeInsert, MultipleInsert) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int vals[] = {5, 3, 7, 1, 9, 4, 6, 2, 8};
  for (int v : vals)
    rbtree_insert(t, a, &vals[v - 1]);

  EXPECT_EQ(rbtree_size(t), 9u);

  for (int v : vals) {
    int key = v;
    EXPECT_NE(rbtree_find(t, &key), nullptr);
    EXPECT_TRUE(rbtree_contains(t, &key));
  }

  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeInsert, DuplicateReplace) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int v1 = 10, v2 = 10;
  rbtree_insert(t, a, &v1);
  void *old = rbtree_insert(t, a, &v2);
  EXPECT_EQ(old, &v1); /* old value returned */
  EXPECT_EQ(rbtree_size(t), 1u);

  int key = 10;
  EXPECT_EQ(rbtree_find(t, &key), &v2); /* new value stored */

  rbtree_free(a, &t);
  delete_allocator(&a);
}

/* ==== Min / Max ==== */

TEST(RbTreeMinMax, Basic) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int vals[] = {5, 3, 7, 1, 9};
  for (int i = 0; i < 5; i++)
    rbtree_insert(t, a, &vals[i]);

  EXPECT_EQ(*(int *)rbtree_min(t), 1);
  EXPECT_EQ(*(int *)rbtree_max(t), 9);

  rbtree_free(a, &t);
  delete_allocator(&a);
}

/* ==== Remove ==== */

TEST(RbTreeRemove, BasicRemove) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int vals[] = {5, 3, 7, 1, 9};
  for (int i = 0; i < 5; i++)
    rbtree_insert(t, a, &vals[i]);

  int key = 3;
  void *removed = rbtree_remove(t, &key);
  EXPECT_EQ(removed, &vals[1]);
  EXPECT_EQ(rbtree_size(t), 4u);
  EXPECT_FALSE(rbtree_contains(t, &key));

  /* Other elements still present */
  for (int v : {5, 7, 1, 9}) {
    int k = v;
    EXPECT_TRUE(rbtree_contains(t, &k));
  }

  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeRemove, RemoveNotFound) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int val = 1;
  rbtree_insert(t, a, &val);

  int key = 99;
  EXPECT_EQ(rbtree_remove(t, &key), nullptr);
  EXPECT_EQ(rbtree_size(t), 1u);

  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeRemove, RemoveRoot) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int val = 42;
  rbtree_insert(t, a, &val);
  EXPECT_EQ(rbtree_size(t), 1u);

  int key = 42;
  void *removed = rbtree_remove(t, &key);
  EXPECT_EQ(removed, &val);
  EXPECT_EQ(rbtree_size(t), 0u);
  EXPECT_TRUE(rbtree_is_empty(t));

  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeRemove, RemoveMinAndMax) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int vals[] = {5, 3, 7, 1, 9};
  for (int i = 0; i < 5; i++)
    rbtree_insert(t, a, &vals[i]);

  int k1 = 1;
  rbtree_remove(t, &k1);
  EXPECT_EQ(*(int *)rbtree_min(t), 3);

  int k9 = 9;
  rbtree_remove(t, &k9);
  EXPECT_EQ(*(int *)rbtree_max(t), 7);

  rbtree_free(a, &t);
  delete_allocator(&a);
}

/* ==== Owned elements ==== */

TEST(RbTreeOwned, FreeOwnedElements) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int_box, true);

  for (int i = 0; i < 5; i++) {
    int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
    b->value = i * 10;
    rbtree_insert(t, a, b);
  }
  EXPECT_EQ(rbtree_size(t), 5u);

  /* rbtree_free with owns=true should free all elements */
  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeOwned, NonOwnedNotFreed) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int_box, false);

  int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b->value = 42;
  rbtree_insert(t, a, b);

  rbtree_free(a, &t);
  /* b is still valid because owns=false */
  EXPECT_EQ(b->value, 42);
  allocator_free(a, (void **)&b);
  delete_allocator(&a);
}

/* ==== Clone ==== */

TEST(RbTreeClone, ShallowClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int val = 42;
  rbtree_insert(t, a, &val);

  void *t_ptr = t;
  rbtree_t *cloned = (rbtree_t *)allocator_clone(a, &t_ptr);
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(rbtree_size(cloned), 1u);

  /* Shallow: same pointer */
  int key = 42;
  EXPECT_EQ(rbtree_find(cloned, &key), rbtree_find(t, &key));

  rbtree_free(a, &cloned);
  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeClone, DeepClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_str_box, true);

  for (const char *s : {"alpha", "beta", "gamma"}) {
    str_box_t *b = (str_box_t *)allocator_new(a, &str_box_class, 1);
    size_t len = strlen(s) + 1;
    b->str = (char *)allocator_new(a, &byte_class, len);
    memcpy(b->str, s, len);
    rbtree_insert(t, a, b);
  }

  void *t_ptr = t;
  rbtree_t *cloned = (rbtree_t *)allocator_clone(a, &t_ptr);
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(rbtree_size(cloned), 3u);

  /* Deep: different pointers, same content */
  str_box_t key_s = {.str = (char *)"beta"};
  void *orig = rbtree_find(t, &key_s);
  void *copy = rbtree_find(cloned, &key_s);
  ASSERT_NE(orig, nullptr);
  ASSERT_NE(copy, nullptr);
  EXPECT_NE(orig, copy);
  EXPECT_STREQ(((str_box_t *)orig)->str, ((str_box_t *)copy)->str);
  EXPECT_NE(((str_box_t *)orig)->str, ((str_box_t *)copy)->str);

  rbtree_free(a, &cloned);
  rbtree_free(a, &t);
  delete_allocator(&a);
}

/* ==== Move ==== */

TEST(RbTreeMove, TransferOwnership) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  int val = 42;
  rbtree_insert(t, a, &val);

  rbtree_t *moved = (rbtree_t *)allocator_move(a, (void **)&t);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(t, nullptr);
  EXPECT_EQ(rbtree_size(moved), 1u);

  int key = 42;
  EXPECT_NE(rbtree_find(moved, &key), nullptr);

  rbtree_free(a, &moved);
  delete_allocator(&a);
}

/* ==== Stress test: sorted insertion / removal ==== */

TEST(RbTreeStress, SortedInsertAndRemove) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  const int N = 1000;
  std::vector<int> vals(N);
  for (int i = 0; i < N; i++)
    vals[i] = i;

  /* Insert in sorted order (worst case for BST, fine for RB-tree) */
  for (int i = 0; i < N; i++)
    rbtree_insert(t, a, &vals[i]);
  EXPECT_EQ(rbtree_size(t), (size_t)N);

  EXPECT_EQ(*(int *)rbtree_min(t), 0);
  EXPECT_EQ(*(int *)rbtree_max(t), N - 1);

  /* Remove even numbers */
  for (int i = 0; i < N; i += 2) {
    rbtree_remove(t, &vals[i]);
  }
  EXPECT_EQ(rbtree_size(t), (size_t)(N / 2));

  /* Verify odd numbers still present */
  for (int i = 1; i < N; i += 2) {
    EXPECT_TRUE(rbtree_contains(t, &vals[i]));
  }

  rbtree_free(a, &t);
  delete_allocator(&a);
}

TEST(RbTreeStress, RandomInsertAndRemove) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  rbtree_t *t = rbtree_new(a, cmp_int, false);

  const int N = 500;
  std::vector<int> vals(N);
  for (int i = 0; i < N; i++)
    vals[i] = i;

  /* Shuffle */
  std::mt19937 rng(42);
  std::shuffle(vals.begin(), vals.end(), rng);

  for (int i = 0; i < N; i++)
    rbtree_insert(t, a, &vals[i]);
  EXPECT_EQ(rbtree_size(t), (size_t)N);

  /* Remove first half */
  for (int i = 0; i < N / 2; i++)
    rbtree_remove(t, &vals[i]);
  EXPECT_EQ(rbtree_size(t), (size_t)(N - N / 2));

  for (int i = N / 2; i < N; i++)
    EXPECT_TRUE(rbtree_contains(t, &vals[i]));
  for (int i = 0; i < N / 2; i++)
    EXPECT_FALSE(rbtree_contains(t, &vals[i]));

  rbtree_free(a, &t);
  delete_allocator(&a);
}

/* ==== Null safety ==== */

TEST(RbTreeNull, NullSafe) {
  EXPECT_EQ(rbtree_size(nullptr), 0u);
  EXPECT_TRUE(rbtree_is_empty(nullptr));
  EXPECT_FALSE(rbtree_owns_element(nullptr));
  EXPECT_EQ(rbtree_min(nullptr), nullptr);
  EXPECT_EQ(rbtree_max(nullptr), nullptr);
  EXPECT_EQ(rbtree_find(nullptr, (void *)1), nullptr);
  EXPECT_FALSE(rbtree_contains(nullptr, (void *)1));
  EXPECT_EQ(rbtree_remove(nullptr, (void *)1), nullptr);
}
