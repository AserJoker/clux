#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "core/allocator.h"
#include "core/panic.h"
#include "core/vec.h"
}

/* ---- Test allocator helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr) { free(ptr); }

/* ---- Panic handler for death / exception tests ---- */

static thread_local std::string g_last_vec_panic;

extern "C" void vec_throw_handler(const char *message) {
  g_last_vec_panic = message;
  throw std::runtime_error(message);
}

class VecPanicTest : public ::testing::Test {
protected:
  panic_handler_t saved_;
  void SetUp() override { saved_ = get_panic_handler(); }
  void TearDown() override { set_panic_handler(saved_); }
};

/* ---- Simple class for owned-element tests ---- */

typedef struct {
  int value;
} int_box_t;

static class_t int_box_class = {
    .name = "int_box",
    .size = sizeof(int_box_t),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- String box for deep-clone tests ---- */

static class_t byte_class = {
    .name = "byte",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

typedef struct {
  char *str;
} str_box_t;

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

/* ==== VecNew / VecWithCapacity ==== */

TEST(VecNew, EmptyVec) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(vec_len(v), 0u);
  EXPECT_EQ(vec_cap(v), 0u);
  EXPECT_TRUE(vec_is_empty(v));
  EXPECT_FALSE(vec_owns_element(v));
  vec_free(a, &v);
  EXPECT_EQ(v, nullptr);
  delete_allocator(&a);
}

TEST(VecNew, OwnedVec) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, true);
  EXPECT_TRUE(vec_owns_element(v));
  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecNew, NullAllocator) { EXPECT_EQ(vec_new(NULL, false), nullptr); }

TEST(VecWithCapacity, PreAllocate) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_with_capacity(a, false, 16);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(vec_len(v), 0u);
  EXPECT_GE(vec_cap(v), 16u);
  EXPECT_TRUE(vec_is_empty(v));
  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecWithCapacity, ZeroCapacity) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_with_capacity(a, false, 0);
  ASSERT_NE(v, nullptr);
  EXPECT_EQ(vec_cap(v), 0u);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecFree ==== */

TEST(VecFree, NullSafe) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_free(a, nullptr); // no-op
  vec_t *null_vec = nullptr;
  vec_free(a, &null_vec);       // no-op
  vec_free(nullptr, &null_vec); // no-op
  delete_allocator(&a);
}

TEST(VecFree, OwnedElementsFreed) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, true);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 10;
  int_box_t *b2 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b2->value = 20;

  vec_push(v, a, b1);
  vec_push(v, a, b2);
  EXPECT_EQ(vec_len(v), 2u);

  // vec_free with owns=true should free both int_box objects
  vec_free(a, &v);
  EXPECT_EQ(v, nullptr);
  delete_allocator(&a);
}

TEST(VecFree, NonOwnedElementsNotFreed) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 10;
  vec_push(v, a, b1);

  vec_free(a, &v);
  // b1 is still valid because owns=false
  EXPECT_EQ(b1->value, 10);
  // clean up manually
  allocator_free(a, (void **)&b1);
  delete_allocator(&a);
}

/* ==== VecPush / VecPop ==== */

TEST(VecPushPop, BasicSequence) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int values[] = {1, 2, 3};
  for (int i = 0; i < 3; i++) {
    int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
    b->value = values[i];
    vec_push(v, a, b);
  }
  EXPECT_EQ(vec_len(v), 3u);
  EXPECT_FALSE(vec_is_empty(v));

  // Pop in LIFO order
  int_box_t *last = (int_box_t *)vec_pop(v);
  EXPECT_EQ(last->value, 3);
  EXPECT_EQ(vec_len(v), 2u);

  int_box_t *second = (int_box_t *)vec_pop(v);
  EXPECT_EQ(second->value, 2);

  int_box_t *first = (int_box_t *)vec_pop(v);
  EXPECT_EQ(first->value, 1);

  EXPECT_EQ(vec_len(v), 0u);
  EXPECT_TRUE(vec_is_empty(v));
  EXPECT_EQ(vec_pop(v), nullptr);

  allocator_free(a, (void **)&last);
  allocator_free(a, (void **)&second);
  allocator_free(a, (void **)&first);
  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecPushPop, NullValueIgnored) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);
  vec_push(v, a, nullptr);
  EXPECT_EQ(vec_len(v), 0u);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecGet / VecSet / VecFirst / VecLast ==== */

TEST(VecAccess, GetAndSet) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 42;
  vec_push(v, a, b1);

  EXPECT_EQ(((int_box_t *)vec_get(v, 0))->value, 42);
  EXPECT_EQ(vec_get(v, 999), nullptr); // out of bounds

  int_box_t *b2 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b2->value = 99;
  void *old = vec_set(v, 0, b2);
  EXPECT_EQ(((int_box_t *)old)->value, 42);
  EXPECT_EQ(((int_box_t *)vec_get(v, 0))->value, 99);

  allocator_free(a, (void **)&old);
  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecAccess, FirstAndLast) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  EXPECT_EQ(vec_first(v), nullptr);
  EXPECT_EQ(vec_last(v), nullptr);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 1;
  vec_push(v, a, b1);

  int_box_t *b2 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b2->value = 2;
  vec_push(v, a, b2);

  EXPECT_EQ(((int_box_t *)vec_first(v))->value, 1);
  EXPECT_EQ(((int_box_t *)vec_last(v))->value, 2);

  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecInsert / VecRemove ==== */

TEST(VecInsertRemove, InsertAtBeginning) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b2 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b2->value = 2;
  vec_push(v, a, b2);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 1;
  vec_insert(v, a, 0, b1);

  EXPECT_EQ(vec_len(v), 2u);
  EXPECT_EQ(((int_box_t *)vec_get(v, 0))->value, 1);
  EXPECT_EQ(((int_box_t *)vec_get(v, 1))->value, 2);

  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecInsertRemove, InsertAtEnd) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 1;
  vec_push(v, a, b1);

  int_box_t *b2 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b2->value = 2;
  vec_insert(v, a, 1, b2);

  EXPECT_EQ(((int_box_t *)vec_get(v, 1))->value, 2);

  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecInsertRemove, RemoveMiddle) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  for (int i = 0; i < 3; i++) {
    int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
    b->value = i + 1;
    vec_push(v, a, b);
  }

  void *removed = vec_remove(v, 1);
  EXPECT_EQ(((int_box_t *)removed)->value, 2);
  EXPECT_EQ(vec_len(v), 2u);
  EXPECT_EQ(((int_box_t *)vec_get(v, 0))->value, 1);
  EXPECT_EQ(((int_box_t *)vec_get(v, 1))->value, 3);

  allocator_free(a, (void **)&removed);
  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecInsertRemove, RemoveOutOfBounds) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);
  EXPECT_EQ(vec_remove(v, 0), nullptr);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecSwapRemove ==== */

TEST(VecSwapRemove, Basic) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  for (int i = 0; i < 5; i++) {
    int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
    b->value = i;
    vec_push(v, a, b);
  }

  // swap_remove at index 1: should replace data[1] with data[4]
  void *removed = vec_swap_remove(v, 1);
  EXPECT_EQ(((int_box_t *)removed)->value, 1);
  EXPECT_EQ(vec_len(v), 4u);
  EXPECT_EQ(((int_box_t *)vec_get(v, 1))->value,
            4); // last element moved to index 1

  allocator_free(a, (void **)&removed);
  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecSwapRemove, LastElement) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b->value = 42;
  vec_push(v, a, b);

  void *removed = vec_swap_remove(v, 0);
  EXPECT_EQ(((int_box_t *)removed)->value, 42);
  EXPECT_EQ(vec_len(v), 0u);

  allocator_free(a, (void **)&removed);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecReserve / VecShrinkToFit ==== */

TEST(VecCapacity, Reserve) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  vec_reserve(v, a, 100);
  EXPECT_GE(vec_cap(v), 100u);
  EXPECT_EQ(vec_len(v), 0u);

  // Second reserve with smaller amount is no-op
  size_t cap_before = vec_cap(v);
  vec_reserve(v, a, 10);
  EXPECT_EQ(vec_cap(v), cap_before);

  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecCapacity, ShrinkToFit) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_with_capacity(a, false, 100);

  for (int i = 0; i < 5; i++) {
    int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
    b->value = i;
    vec_push(v, a, b);
  }

  EXPECT_GE(vec_cap(v), 100u);
  vec_shrink_to_fit(v, a);
  EXPECT_EQ(vec_cap(v), 5u);
  EXPECT_EQ(vec_len(v), 5u);

  // Verify data integrity after shrink
  for (int i = 0; i < 5; i++) {
    EXPECT_EQ(((int_box_t *)vec_get(v, i))->value, i);
  }

  vec_free(a, &v);
  delete_allocator(&a);
}

TEST(VecCapacity, ShrinkToFitEmpty) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_with_capacity(a, false, 10);
  vec_shrink_to_fit(v, a);
  EXPECT_EQ(vec_cap(v), 0u);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== Growth policy ==== */

TEST(VecCapacity, AutoGrowth) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  size_t prev_cap = 0;
  for (int i = 0; i < 100; i++) {
    int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
    b->value = i;
    vec_push(v, a, b);

    if (vec_cap(v) > prev_cap) {
      // Capacity should at least double
      if (prev_cap > 0) {
        EXPECT_GE(vec_cap(v), prev_cap * 2);
      }
      prev_cap = vec_cap(v);
    }
  }
  EXPECT_EQ(vec_len(v), 100u);

  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== allocator_move on vec ==== */

TEST(VecMove, TransferOwnership) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b->value = 77;
  vec_push(v, a, b);

  vec_t *moved = (vec_t *)allocator_move(a, (void **)&v);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(v, nullptr); // source nullified
  EXPECT_EQ(vec_len(moved), 1u);
  EXPECT_EQ(((int_box_t *)vec_get(moved, 0))->value, 77);

  vec_free(a, &moved);
  delete_allocator(&a);
}

/* ==== allocator_clone on vec (owns=false: shallow) ==== */

TEST(VecClone, ShallowClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b->value = 42;
  vec_push(v, a, b);

  void *v_ptr = v;
  vec_t *cloned = (vec_t *)allocator_clone(a, (void **)&v_ptr);
  ASSERT_NE(cloned, nullptr);

  // Source unchanged
  EXPECT_EQ(vec_len(v), 1u);

  // Both point to the same element (shallow)
  EXPECT_EQ(vec_get(cloned, 0), vec_get(v, 0));

  vec_free(a, &cloned); // owns=false, won't free element
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== allocator_clone on vec (owns=true: deep) ==== */

TEST(VecClone, DeepClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, true);

  str_box_t *s1 = (str_box_t *)allocator_new(a, &str_box_class, 1);
  s1->str = (char *)allocator_new(a, &byte_class, 6);
  memcpy(s1->str, "hello", 6);
  vec_push(v, a, s1);

  str_box_t *s2 = (str_box_t *)allocator_new(a, &str_box_class, 1);
  s2->str = (char *)allocator_new(a, &byte_class, 6);
  memcpy(s2->str, "world", 6);
  vec_push(v, a, s2);

  void *v_ptr = v;
  vec_t *cloned = (vec_t *)allocator_clone(a, (void **)&v_ptr);
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(vec_len(cloned), 2u);

  // Elements are deep-copied: different pointers, same content
  str_box_t *c0 = (str_box_t *)vec_get(cloned, 0);
  str_box_t *c1 = (str_box_t *)vec_get(cloned, 1);
  ASSERT_NE(c0, nullptr);
  ASSERT_NE(c1, nullptr);
  EXPECT_NE(c0->str, s1->str); // different memory
  EXPECT_STREQ(c0->str, "hello");
  EXPECT_NE(c1->str, s2->str);
  EXPECT_STREQ(c1->str, "world");

  vec_free(a, &cloned); // frees cloned elements
  vec_free(a, &v);      // frees original elements
  delete_allocator(&a);
}

/* ==== Move + dispose: moved-from vec must not free elements ==== */

TEST(VecMove, MovedFromVecSafeDispose) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, true);

  str_box_t *s = (str_box_t *)allocator_new(a, &str_box_class, 1);
  s->str = (char *)allocator_new(a, &byte_class, 6);
  memcpy(s->str, "test", 5);
  vec_push(v, a, s);

  // allocator_move will: move_fn (transfer data pointer) then allocator_free on source
  // Source's dispose_fn should see data=NULL and skip freeing elements
  vec_t *moved = (vec_t *)allocator_move(a, (void **)&v);
  EXPECT_EQ(v, nullptr);
  EXPECT_EQ(vec_len(moved), 1u);

  // Element still accessible
  str_box_t *m0 = (str_box_t *)vec_get(moved, 0);
  EXPECT_STREQ(m0->str, "test");

  vec_free(a, &moved);
  delete_allocator(&a);
}

/* ==== VecSet returns old value ==== */

TEST(VecAccess, SetOldValue) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b1 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b1->value = 10;
  vec_push(v, a, b1);

  int_box_t *b2 = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b2->value = 20;
  void *old = vec_set(v, 0, b2);

  EXPECT_EQ(((int_box_t *)old)->value, 10);
  EXPECT_EQ(((int_box_t *)vec_get(v, 0))->value, 20);

  allocator_free(a, (void **)&old);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecInsert out of bounds panics ==== */

TEST_F(VecPanicTest, InsertOutOfBoundsPanics) {
  set_panic_handler(vec_throw_handler);
  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, false);

  int_box_t *b = (int_box_t *)allocator_new(a, &int_box_class, 1);
  b->value = 1;

  try {
    vec_insert(v, a, 5, b); // index 5 > len 0
    FAIL() << "should have panicked";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("out of bounds"), std::string::npos);
  }

  allocator_free(a, (void **)&b);
  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== allocator_clone on vec without clone_fn panics ==== */

TEST_F(VecPanicTest, CloneVecWithNonCloneableElementPanics) {
  set_panic_handler(vec_throw_handler);

  // Create a class without clone_fn
  class_t no_clone_class = {
      .name = "no_clone",
      .size = sizeof(int),
      .move_fn = default_move,
      .clone_fn = NULL,
      .dispose_fn = NULL,
  };

  allocator_t *a = create_allocator(test_alloc, test_free);
  vec_t *v = vec_new(a, true);

  void *elem = allocator_new(a, &no_clone_class, 1);
  vec_push(v, a, elem);

  try {
    void *v_ptr = v;
    allocator_clone(a, (void **)&v_ptr);
    FAIL() << "should have panicked on non-cloneable element";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("does not support clone"),
              std::string::npos);
  }

  vec_free(a, &v);
  delete_allocator(&a);
}

/* ==== VecLen / VecCap / VecIsEmpty on NULL ==== */

TEST(VecAccess, NullSafe) {
  EXPECT_EQ(vec_len(nullptr), 0u);
  EXPECT_EQ(vec_cap(nullptr), 0u);
  EXPECT_TRUE(vec_is_empty(nullptr));
  EXPECT_FALSE(vec_owns_element(nullptr));
  EXPECT_EQ(vec_get(nullptr, 0), nullptr);
  EXPECT_EQ(vec_first(nullptr), nullptr);
  EXPECT_EQ(vec_last(nullptr), nullptr);
  EXPECT_EQ(vec_pop(nullptr), nullptr);
  EXPECT_EQ(vec_remove(nullptr, 0), nullptr);
  EXPECT_EQ(vec_swap_remove(nullptr, 0), nullptr);
}
