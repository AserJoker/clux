#include <gtest/gtest.h>
#include <stdexcept>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "core/allocator.h"
#include "core/panic.h"
}

/* ---- Test helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr) { free(ptr); }

/* Tracking allocator for verifying alloc/free call counts */
struct TrackState {
  int alloc_count;
  int free_count;
};

static TrackState g_track = {0, 0};

static void *track_alloc(size_t size) {
  g_track.alloc_count++;
  return malloc(size);
}

static void track_free(void *ptr) {
  g_track.free_count++;
  free(ptr);
}

/* A class_t with NULL move/clone/dispose — only valid for new/free */
static class_t no_callback_class = {
    .name = "no_callback",
    .size = sizeof(int),
    .move_fn = nullptr,
    .clone_fn = nullptr,
    .dispose_fn = nullptr,
};

/* Sample class_t for int objects (with default_move/default_clone) */
static class_t int_class = {
    .name = "int",
    .size = sizeof(int),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = nullptr,
};

/* byte class for variable-length buffers (e.g. strings) */
static class_t byte_class = {
    .name = "byte",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = nullptr,
};

/* ---- Dispose tracking ---- */

static int g_dispose_call_count = 0;

static void counting_dispose(void *self, allocator_t *allocator) {
  (void)self;
  (void)allocator;
  g_dispose_call_count++;
}

static class_t dispose_class = {
    .name = "dispose_int",
    .size = sizeof(int),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = counting_dispose,
};

/* ---- Custom move/clone for string pointer ---- */

struct string_box {
  char *str;
};

static void string_move(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  auto *dst = static_cast<string_box *>(self);
  auto *src = static_cast<string_box *>(another);
  dst->str = src->str;
  src->str = nullptr;
}

static void string_clone(void *self, allocator_t *allocator, void *another) {
  auto *dst = static_cast<string_box *>(self);
  auto *src = static_cast<string_box *>(another);
  if (src->str) {
    size_t len = strlen(src->str) + 1;
    dst->str = static_cast<char *>(allocator_new(allocator, &byte_class, len));
    memcpy(dst->str, src->str, len);
  } else {
    dst->str = nullptr;
  }
}

static void string_dispose(void *self, allocator_t *allocator) {
  auto *box = static_cast<string_box *>(self);
  if (box->str) {
    void *p = box->str;
    allocator_free(allocator, &p);
    box->str = nullptr;
  }
}

static class_t string_class = {
    .name = "string_box",
    .size = sizeof(string_box),
    .move_fn = string_move,
    .clone_fn = string_clone,
    .dispose_fn = string_dispose,
};

/* ---- Panic handler for testing ---- */

extern "C" void alloc_throw_handler(const char *message) {
  throw std::runtime_error(message);
}

/* ---- Fixture: save/restore panic handler ---- */

class AllocatorPanicTest : public ::testing::Test {
protected:
  panic_handler_t saved_panic_;

  void SetUp() override { saved_panic_ = get_panic_handler(); }

  void TearDown() override { set_panic_handler(saved_panic_); }
};

/* ========================================================================= */
/* Test suites                                                               */
/* ========================================================================= */

/* ---- CreateDelete ---- */

TEST(CreateDelete, ValidAllocator) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  ASSERT_NE(a, nullptr);
  delete_allocator(&a);
  EXPECT_EQ(a, nullptr);
}

TEST(CreateDelete, NullAllocFn) {
  EXPECT_EQ(create_allocator(nullptr, test_free), nullptr);
}

TEST(CreateDelete, NullFreeFn) {
  EXPECT_EQ(create_allocator(test_alloc, nullptr), nullptr);
}

TEST(CreateDelete, BothNullFn) {
  EXPECT_EQ(create_allocator(nullptr, nullptr), nullptr);
}

TEST(CreateDelete, NullDoublePtr) {
  delete_allocator(nullptr); // no crash
}

TEST(CreateDelete, NullPtr) {
  allocator_t *p = nullptr;
  delete_allocator(&p); // no crash
  EXPECT_EQ(p, nullptr);
}

TEST(CreateDelete, DeleteAndNullify) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  ASSERT_NE(a, nullptr);
  delete_allocator(&a);
  EXPECT_EQ(a, nullptr);
}

/* ---- AllocatorNew ---- */

TEST(AllocatorNew, SingleInt) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new(a, &int_class, 1);
  ASSERT_NE(data, nullptr);

  int *p = static_cast<int *>(data);
  EXPECT_EQ(*p, 0); // zero-initialized
  *p = 42;
  EXPECT_EQ(*p, 42);

  allocator_free(a, &data);
  EXPECT_EQ(data, nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNew, MultipleInts) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  constexpr size_t count = 5;
  void *data = allocator_new(a, &int_class, count);
  ASSERT_NE(data, nullptr);

  int *p = static_cast<int *>(data);
  for (size_t i = 0; i < count; i++) {
    EXPECT_EQ(p[i], 0);
    p[i] = static_cast<int>(i * 10);
  }
  for (size_t i = 0; i < count; i++) {
    EXPECT_EQ(p[i], static_cast<int>(i * 10));
  }

  allocator_free(a, &data);
  delete_allocator(&a);
}

TEST(AllocatorNew, ZeroCount) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_new(a, &int_class, 0), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNew, NullAllocator) {
  EXPECT_EQ(allocator_new(nullptr, &int_class, 1), nullptr);
}

TEST(AllocatorNew, NullClass) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_new(a, nullptr, 1), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNew, ClassZeroSize) {
  class_t zero_class = {
      .name = "zero",
      .size = 0,
      .move_fn = nullptr,
      .clone_fn = nullptr,
      .dispose_fn = nullptr,
  };
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_new(a, &zero_class, 1), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNew, GetClassReturnsCorrect) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new(a, &int_class, 1);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(allocator_get_class(data), &int_class);
  allocator_free(a, &data);
  delete_allocator(&a);
}

TEST(AllocatorNew, GetCountReturnsCorrect) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new(a, &int_class, 3);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(allocator_get_count(data), 3u);
  allocator_free(a, &data);
  delete_allocator(&a);
}

TEST(AllocatorNew, GetClassNullData) {
  EXPECT_EQ(allocator_get_class(nullptr), nullptr);
}

TEST(AllocatorNew, GetCountNullData) {
  EXPECT_EQ(allocator_get_count(nullptr), 0u);
}

/* ---- AllocatorNewEx ---- */

TEST(AllocatorNewEx, Basic) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new_ex(a, "point", sizeof(double), default_move,
                                default_clone, nullptr, 1);
  ASSERT_NE(data, nullptr);

  double *p = static_cast<double *>(data);
  EXPECT_DOUBLE_EQ(*p, 0.0);
  *p = 3.14;
  EXPECT_DOUBLE_EQ(*p, 3.14);

  allocator_free(a, &data);
  EXPECT_EQ(data, nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNewEx, GetClassInfo) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new_ex(a, "point", sizeof(double), default_move,
                                default_clone, nullptr, 2);
  ASSERT_NE(data, nullptr);

  const class_t *clazz = allocator_get_class(data);
  ASSERT_NE(clazz, nullptr);
  EXPECT_STREQ(clazz->name, "point");
  EXPECT_EQ(clazz->size, sizeof(double));
  EXPECT_EQ(clazz->move_fn, default_move);
  EXPECT_EQ(clazz->clone_fn, default_clone);
  EXPECT_EQ(clazz->dispose_fn, nullptr);
  EXPECT_EQ(allocator_get_count(data), 2u);

  allocator_free(a, &data);
  delete_allocator(&a);
}

TEST(AllocatorNewEx, NullMoveCloneDispose) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new_ex(a, "raw", sizeof(int), nullptr, nullptr,
                                nullptr, 1);
  ASSERT_NE(data, nullptr);

  const class_t *clazz = allocator_get_class(data);
  ASSERT_NE(clazz, nullptr);
  EXPECT_EQ(clazz->move_fn, nullptr);
  EXPECT_EQ(clazz->clone_fn, nullptr);
  EXPECT_EQ(clazz->dispose_fn, nullptr);

  allocator_free(a, &data);
  delete_allocator(&a);
}

TEST(AllocatorNewEx, ZeroSize) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_new_ex(a, "zero", 0, nullptr, nullptr, nullptr, 1),
            nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNewEx, ZeroCount) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_new_ex(a, "zero", sizeof(int), nullptr, nullptr, nullptr,
                             0),
            nullptr);
  delete_allocator(&a);
}

TEST(AllocatorNewEx, NullAllocator) {
  EXPECT_EQ(allocator_new_ex(nullptr, "zero", sizeof(int), nullptr, nullptr,
                             nullptr, 1),
            nullptr);
}

/* ---- AllocatorFree ---- */

TEST(AllocatorFree, FreeAndNullify) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new(a, &int_class, 1);
  ASSERT_NE(data, nullptr);
  allocator_free(a, &data);
  EXPECT_EQ(data, nullptr);
  delete_allocator(&a);
}

TEST(AllocatorFree, FreeNullData) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *p = nullptr;
  allocator_free(a, &p); // no crash
  EXPECT_EQ(p, nullptr);
  delete_allocator(&a);
}

TEST(AllocatorFree, FreeNullDoublePtr) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  allocator_free(a, nullptr); // no crash
  delete_allocator(&a);
}

TEST(AllocatorFree, FreeNullAllocator) {
  int dummy = 0;
  void *p = &dummy;
  allocator_free(nullptr, &p); // no crash, pointer unchanged
}

TEST(AllocatorFree, CallsDispose) {
  g_dispose_call_count = 0;
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new(a, &dispose_class, 1);
  ASSERT_NE(data, nullptr);
  allocator_free(a, &data);
  EXPECT_EQ(g_dispose_call_count, 1);
  EXPECT_EQ(data, nullptr);
  delete_allocator(&a);
}

TEST(AllocatorFree, DisposeNullSafe) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *data = allocator_new(a, &no_callback_class, 1);
  ASSERT_NE(data, nullptr);
  allocator_free(a, &data); // dispose_fn is nullptr, no crash
  EXPECT_EQ(data, nullptr);
  delete_allocator(&a);
}

/* ---- AllocatorMove ---- */

TEST(AllocatorMove, BasicWithDefaultMove) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *src = allocator_new(a, &int_class, 1);
  ASSERT_NE(src, nullptr);
  *static_cast<int *>(src) = 42;

  void *dst = allocator_move(a, &src);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(*static_cast<int *>(dst), 42);
  EXPECT_EQ(src, nullptr);

  allocator_free(a, &dst);
  delete_allocator(&a);
}

TEST(AllocatorMove, CustomMoveFn) {
  allocator_t *a = create_allocator(test_alloc, test_free);

  void *src = allocator_new(a, &string_class, 1);
  ASSERT_NE(src, nullptr);
  auto *src_box = static_cast<string_box *>(src);
  const char *hello = "hello";
  size_t len = strlen(hello) + 1;
  src_box->str = static_cast<char *>(allocator_new(a, &byte_class, len));
  memcpy(src_box->str, hello, len);

  char *saved_str = src_box->str;

  void *dst = allocator_move(a, &src);
  ASSERT_NE(dst, nullptr);
  auto *dst_box = static_cast<string_box *>(dst);
  EXPECT_EQ(dst_box->str, saved_str); // same pointer — ownership transferred
  EXPECT_STREQ(dst_box->str, "hello");
  EXPECT_EQ(src, nullptr); // source pointer nullified

  allocator_free(a, &dst);
  delete_allocator(&a);
}

TEST(AllocatorMove, NullObject) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *p = nullptr;
  EXPECT_EQ(allocator_move(a, &p), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorMove, NullPtr) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_move(a, nullptr), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorMove, NullAllocator) {
  void *p = nullptr;
  EXPECT_EQ(allocator_move(nullptr, &p), nullptr);
}

TEST_F(AllocatorPanicTest, PanicsOnNullMoveFn) {
  set_panic_handler(alloc_throw_handler);
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *src = allocator_new(a, &no_callback_class, 1);
  ASSERT_NE(src, nullptr);

  try {
    allocator_move(a, &src);
    FAIL() << "allocator_move should panic on NULL move_fn";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("does not support move"), std::string::npos);
    EXPECT_NE(msg.find("no_callback"), std::string::npos);
  }

  allocator_free(a, &src);
  delete_allocator(&a);
}

/* ---- AllocatorClone ---- */

TEST(AllocatorClone, BasicWithDefaultClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *src = allocator_new(a, &int_class, 1);
  ASSERT_NE(src, nullptr);
  *static_cast<int *>(src) = 42;

  void *dst = allocator_clone(a, &src);
  ASSERT_NE(dst, nullptr);
  EXPECT_EQ(*static_cast<int *>(dst), 42);
  EXPECT_EQ(*static_cast<int *>(src), 42); // source unchanged

  *static_cast<int *>(dst) = 100;
  EXPECT_EQ(*static_cast<int *>(src), 42);

  allocator_free(a, &src);
  allocator_free(a, &dst);
  delete_allocator(&a);
}

TEST(AllocatorClone, CustomCloneFn) {
  allocator_t *a = create_allocator(test_alloc, test_free);

  void *src = allocator_new(a, &string_class, 1);
  ASSERT_NE(src, nullptr);
  auto *src_box = static_cast<string_box *>(src);
  const char *world = "world";
  size_t len = strlen(world) + 1;
  src_box->str = static_cast<char *>(allocator_new(a, &byte_class, len));
  memcpy(src_box->str, world, len);

  void *dst = allocator_clone(a, &src);
  ASSERT_NE(dst, nullptr);
  auto *dst_box = static_cast<string_box *>(dst);
  EXPECT_STREQ(dst_box->str, "world");
  EXPECT_STREQ(src_box->str, "world"); // source unchanged

  dst_box->str[0] = 'W';
  EXPECT_STREQ(src_box->str, "world"); // source unaffected

  allocator_free(a, &src);
  allocator_free(a, &dst);
  delete_allocator(&a);
}

TEST(AllocatorClone, NullObject) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *p = nullptr;
  EXPECT_EQ(allocator_clone(a, &p), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorClone, NullPtr) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(allocator_clone(a, nullptr), nullptr);
  delete_allocator(&a);
}

TEST(AllocatorClone, NullAllocator) {
  void *p = nullptr;
  EXPECT_EQ(allocator_clone(nullptr, &p), nullptr);
}

TEST_F(AllocatorPanicTest, PanicsOnNullCloneFn) {
  set_panic_handler(alloc_throw_handler);
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *src = allocator_new(a, &no_callback_class, 1);
  ASSERT_NE(src, nullptr);

  try {
    allocator_clone(a, &src);
    FAIL() << "allocator_clone should panic on NULL clone_fn";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("does not support clone"), std::string::npos);
    EXPECT_NE(msg.find("no_callback"), std::string::npos);
  }

  allocator_free(a, &src);
  delete_allocator(&a);
}

/* ---- OOM panics ---- */

static void *oom_alloc(size_t size) {
  (void)size;
  return nullptr;
}
static void oom_free(void *ptr) { (void)ptr; }

TEST_F(AllocatorPanicTest, OomPanics) {
  set_panic_handler(alloc_throw_handler);
  allocator_t *a = create_allocator(oom_alloc, oom_free);

  try {
    allocator_new(a, &int_class, 1);
    FAIL() << "allocator_new should panic on OOM";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("out of memory"), std::string::npos);
  }

  delete_allocator(&a);
}

TEST_F(AllocatorPanicTest, OomInNewExPanics) {
  set_panic_handler(alloc_throw_handler);
  allocator_t *a = create_allocator(oom_alloc, oom_free);

  try {
    allocator_new_ex(a, "point", sizeof(double), default_move, default_clone,
                    nullptr, 1);
    FAIL() << "allocator_new_ex should panic on OOM";
  } catch (const std::runtime_error &e) {
    std::string msg = e.what();
    EXPECT_NE(msg.find("out of memory"), std::string::npos);
  }

  delete_allocator(&a);
}

/* ---- DefaultCallbacks ---- */

TEST(DefaultCallbacks, DefaultMoveViaAllocatorMove) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *src = allocator_new(a, &int_class, 2);
  ASSERT_NE(src, nullptr);
  int *src_ints = static_cast<int *>(src);
  src_ints[0] = 10;
  src_ints[1] = 20;

  void *dst = allocator_move(a, &src);
  ASSERT_NE(dst, nullptr);
  int *dst_ints = static_cast<int *>(dst);
  EXPECT_EQ(dst_ints[0], 10);
  EXPECT_EQ(dst_ints[1], 20);
  EXPECT_EQ(src, nullptr);

  allocator_free(a, &dst);
  delete_allocator(&a);
}

TEST(DefaultCallbacks, DefaultCloneViaAllocatorClone) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  void *src = allocator_new(a, &int_class, 2);
  ASSERT_NE(src, nullptr);
  int *src_ints = static_cast<int *>(src);
  src_ints[0] = 30;
  src_ints[1] = 40;

  void *dst = allocator_clone(a, &src);
  ASSERT_NE(dst, nullptr);
  int *dst_ints = static_cast<int *>(dst);
  EXPECT_EQ(dst_ints[0], 30);
  EXPECT_EQ(dst_ints[1], 40);
  EXPECT_EQ(src_ints[0], 30);
  EXPECT_EQ(src_ints[1], 40);

  allocator_free(a, &src);
  allocator_free(a, &dst);
  delete_allocator(&a);
}

/* ---- TrackingAllocator ---- */

TEST(TrackingAllocator, CountsMatch) {
  g_track = {0, 0};
  allocator_t *a = create_allocator(track_alloc, track_free);

  void *data = allocator_new(a, &int_class, 1);
  EXPECT_EQ(g_track.alloc_count, 1);
  EXPECT_EQ(g_track.free_count, 0);

  allocator_free(a, &data);
  EXPECT_EQ(g_track.free_count, 1);
  EXPECT_EQ(data, nullptr);

  delete_allocator(&a);
}

/* ---- OwnershipChain (integration) ---- */

TEST(OwnershipChain, AllocateMoveFree) {
  allocator_t *a = create_allocator(test_alloc, test_free);

  void *obj = allocator_new(a, &int_class, 1);
  ASSERT_NE(obj, nullptr);
  *static_cast<int *>(obj) = 123;

  void *moved = allocator_move(a, &obj);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(obj, nullptr);
  EXPECT_EQ(*static_cast<int *>(moved), 123);

  allocator_free(a, &moved);
  EXPECT_EQ(moved, nullptr);
  delete_allocator(&a);
}

TEST(OwnershipChain, AllocateCloneFreeBoth) {
  allocator_t *a = create_allocator(test_alloc, test_free);

  void *obj = allocator_new(a, &int_class, 1);
  ASSERT_NE(obj, nullptr);
  *static_cast<int *>(obj) = 456;

  void *cloned = allocator_clone(a, &obj);
  ASSERT_NE(cloned, nullptr);
  EXPECT_EQ(*static_cast<int *>(obj), 456);
  EXPECT_EQ(*static_cast<int *>(cloned), 456);

  *static_cast<int *>(obj) = 789;
  EXPECT_EQ(*static_cast<int *>(cloned), 456); // clone unaffected

  allocator_free(a, &obj);
  allocator_free(a, &cloned);
  EXPECT_EQ(obj, nullptr);
  EXPECT_EQ(cloned, nullptr);
  delete_allocator(&a);
}

TEST(OwnershipChain, NestedAllocation) {
  allocator_t *a = create_allocator(test_alloc, test_free);

  void *box = allocator_new(a, &string_class, 1);
  ASSERT_NE(box, nullptr);
  auto *b = static_cast<string_box *>(box);
  const char *msg = "nested";
  size_t len = strlen(msg) + 1;
  b->str = static_cast<char *>(allocator_new(a, &byte_class, len));
  memcpy(b->str, msg, len);

  EXPECT_STREQ(b->str, "nested");

  allocator_free(a, &box);
  EXPECT_EQ(box, nullptr);
  delete_allocator(&a);
}
