#include <gtest/gtest.h>
#include "test_common.h"

#include <stdexcept>

extern "C" {
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/scope.h"
#include "vm/type.h"
#include "vm/type_error.h"
#include "core/allocator.h"
#include "core/string.h"
#include "core/strslice.h"
}

/* ---- helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr)     { free(ptr); }

/* 按类型宽度读取有符号整数 */
static int64_t read_sint(const value_t *v) {
    switch (v->type->size) {
        case 1: return (int64_t)*(const int8_t  *)v->data;
        case 2: return (int64_t)*(const int16_t *)v->data;
        case 4: return (int64_t)*(const int32_t *)v->data;
        default: return *(const int64_t *)v->data;
    }
}

/* 按类型宽度读取 double */
static double read_float(const value_t *v) {
    if (v->type->size == sizeof(float))
        return (double)*(const float *)v->data;
    return *(const double *)v->data;
}

/* 构造一个 i32 value（栈上，data 堆分配，未 track 到 scope） */
static value_t make_i32_raw(vm_t *vm, int32_t v) {
    void *data = value_alloc_data_copy(vm->alloc, vm->type_i32, &v);
    return value_make(vm->type_i32, data);
}

/* 构造一个 bool value（栈上，data 堆分配，未 track 到 scope） */
[[maybe_unused]] static value_t make_bool_raw(vm_t *vm, bool v) {
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &v);
    return value_make(vm->type_bool, data);
}

/* 构造一个 str value（栈上，data 堆分配，未 track 到 scope） */
static value_t make_str_raw(vm_t *vm, const char *s) {
    string_t *str = string_from_cstr(vm->alloc, s);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_str, &str);
    return value_make(vm->type_str, data);
}

/* ================================================================ */
/* 1. VM 生命周期                                                    */
/* ================================================================ */

class VmLifecycle : public ::testing::Test {
protected:
    allocator_t *alloc = nullptr;
    vm_t        *vm    = nullptr;

    void SetUp() override {
        alloc = create_allocator(test_alloc, test_free);
        vm    = vm_new(alloc);
    }
    void TearDown() override {
        vm_destroy(&vm);
        EXPECT_EQ(vm, nullptr);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
    }
};

TEST_F(VmLifecycle, CreateDestroy) {
    ASSERT_NE(vm, nullptr);
}

TEST_F(VmLifecycle, ScopeChainInitial) {
    /* vm_new 后: global -> root -> current(root) */
    EXPECT_EQ(vm->global_scope, vm->root_scope->parent);
    EXPECT_EQ(vm->current_scope, vm->root_scope);
}

TEST_F(VmLifecycle, PushPopScope) {
    vm_push_scope(vm);
    EXPECT_NE(vm->current_scope, vm->root_scope);
    EXPECT_EQ(scope_parent(vm->current_scope), vm->root_scope);

    vm_pop_scope(vm);
    EXPECT_EQ(vm->current_scope, vm->root_scope);
}

TEST_F(VmLifecycle, NestedScopes) {
    vm_push_scope(vm);
    scope_t *s1 = vm->current_scope;
    vm_push_scope(vm);
    scope_t *s2 = vm->current_scope;
    EXPECT_EQ(scope_parent(s2), s1);

    vm_pop_scope(vm);
    EXPECT_EQ(vm->current_scope, s1);
    vm_pop_scope(vm);
    EXPECT_EQ(vm->current_scope, vm->root_scope);
}

/* pop global scope 应 panic — panic 调用 abort()，用 EXPECT_DEATH 测试 */
TEST_F(VmLifecycle, PopGlobalPanics) {
    EXPECT_DEATH({
        vm->current_scope = vm->global_scope;
        vm_pop_scope(vm);
    }, "cannot pop the global scope");
}

/* ================================================================ */
/* 2. 内置类型注册表 (type_find)                                     */
/* ================================================================ */

class VmBuiltinTypes : public ::testing::Test {
protected:
    allocator_t *alloc = nullptr;
    vm_t        *vm    = nullptr;

    void SetUp() override {
        alloc = create_allocator(test_alloc, test_free);
        vm    = vm_new(alloc);
    }
    void TearDown() override {
        vm_destroy(&vm);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
    }
};

TEST_F(VmBuiltinTypes, AllIntTypesRegistered) {
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("i8")),  vm->type_i8);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("i16")), vm->type_i16);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("i32")), vm->type_i32);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("i64")), vm->type_i64);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("u8")),  vm->type_u8);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("u16")), vm->type_u16);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("u32")), vm->type_u32);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("u64")), vm->type_u64);
}

TEST_F(VmBuiltinTypes, FloatTypesRegistered) {
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("f32")), vm->type_f32);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("f64")), vm->type_f64);
}

TEST_F(VmBuiltinTypes, OtherTypesRegistered) {
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("bool")), vm->type_bool);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("str")),  vm->type_str);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("void")), vm->type_void);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("type")), vm->type_type);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("func")), vm->type_func);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("error")), vm->type_error);
}

TEST_F(VmBuiltinTypes, UnknownTypeReturnsNull) {
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("nonexistent")), nullptr);
    EXPECT_EQ(type_find(vm, STRSLICE_LIT("")), nullptr);
}

TEST_F(VmBuiltinTypes, TypeSizeAndAlign) {
    EXPECT_EQ(vm->type_i8->size,   sizeof(int8_t));
    EXPECT_EQ(vm->type_i8->align,  alignof(int8_t));
    EXPECT_EQ(vm->type_i32->size,  sizeof(int32_t));
    EXPECT_EQ(vm->type_i32->align, alignof(int32_t));
    EXPECT_EQ(vm->type_i64->size,  sizeof(int64_t));
    EXPECT_EQ(vm->type_i64->align, alignof(int64_t));
    EXPECT_EQ(vm->type_u8->size,   sizeof(uint8_t));
    EXPECT_EQ(vm->type_u32->size,  sizeof(uint32_t));
    EXPECT_EQ(vm->type_u64->size,  sizeof(uint64_t));
    EXPECT_EQ(vm->type_f32->size,  sizeof(float));
    EXPECT_EQ(vm->type_f32->align, alignof(float));
    EXPECT_EQ(vm->type_f64->size,  sizeof(double));
    EXPECT_EQ(vm->type_f64->align, alignof(double));
    EXPECT_EQ(vm->type_bool->size, sizeof(bool));
    EXPECT_EQ(vm->type_void->size, 0u);
    EXPECT_EQ(vm->type_str->size,  sizeof(string_t *));
    EXPECT_EQ(vm->type_func->size, sizeof(func_t *));
}

TEST_F(VmBuiltinTypes, TypeEqIsPointerIdentity) {
    EXPECT_TRUE(type_eq(vm->type_i32, vm->type_i32));
    EXPECT_FALSE(type_eq(vm->type_i32, vm->type_i64));
}

TEST_F(VmBuiltinTypes, TypeAsValue) {
    value_t tv = type_as_value(vm, vm->type_i32);
    EXPECT_EQ(tv.type, vm->type_type);
    EXPECT_EQ(*(const type_t **)tv.data, vm->type_i32);
    /* type_as_value 未 track，手动释放 data 避免 leak */
    value_dispose(vm, &tv);
}

/* ================================================================ */
/* 3. Value 核心机制                                                 */
/* ================================================================ */

class ValueCore : public ::testing::Test {
protected:
    allocator_t *alloc = nullptr;
    vm_t        *vm    = nullptr;

    void SetUp() override {
        alloc = create_allocator(test_alloc, test_free);
        vm    = vm_new(alloc);
    }
    void TearDown() override {
        vm_destroy(&vm);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
    }
};

/* ---- value_make / value_is_void ---- */

TEST_F(ValueCore, ValueMakeBasic) {
    value_t v = make_i32_raw(vm, 42);
    EXPECT_EQ(v.type, vm->type_i32);
    EXPECT_NE(v.data, nullptr);
    EXPECT_EQ(read_sint(&v), 42);
    /* raw value 未 track，需手动释放 */
    value_dispose(vm, &v);
}

TEST_F(ValueCore, ValueIsVoid) {
    value_t v = { nullptr, nullptr };
    EXPECT_TRUE(value_is_void(&v));

    value_t vi = make_i32_raw(vm, 1);
    EXPECT_FALSE(value_is_void(&vi));
    value_dispose(vm, &vi);
}

/* ---- value_clone auto-track ---- */

TEST_F(ValueCore, CloneAutoTracksToCurrentScope) {
    value_t raw = make_i32_raw(vm, 100);
    value_t cloned = value_clone(vm, raw);

    EXPECT_EQ(cloned.type, vm->type_i32);
    EXPECT_EQ(read_sint(&cloned), 100);

    /* clone 后应自动 track 到 current_scope->owned */
    /* pop_scope 时应正确销毁 cloned，不泄漏 */
    vm_push_scope(vm);
    value_t cloned2 = value_clone(vm, raw);
    EXPECT_EQ(read_sint(&cloned2), 100);
    vm_pop_scope(vm); /* 销毁 cloned2 */

    /* 释放 raw */
    value_dispose(vm, &raw);
}

TEST_F(ValueCore, CloneProducesIndependentCopy) {
    value_t raw = make_i32_raw(vm, 7);
    value_t cloned = value_clone(vm, raw);

    /* 修改 cloned 的 data 不影响 raw（独立内存） */
    *(int32_t *)cloned.data = 999;
    EXPECT_EQ(read_sint(&raw), 7);
    EXPECT_EQ(read_sint(&cloned), 999);

    value_dispose(vm, &raw);
    /* cloned 被 track 到 root_scope，vm_destroy 时释放 */
}

TEST_F(ValueCore, CloneVoidReturnsVoid) {
    value_t v = { nullptr, nullptr };
    value_t cloned = value_clone(vm, v);
    EXPECT_EQ(cloned.type, nullptr);
    EXPECT_EQ(cloned.data, nullptr);
}

/* ---- value_dispose ---- */

TEST_F(ValueCore, DisposeNullIsSafe) {
    value_t *vp = nullptr;
    value_dispose(vm, vp); /* 不应崩溃 */

    value_t v = { nullptr, nullptr };
    value_dispose(vm, &v); /* type==NULL，no-op */
}

TEST_F(ValueCore, DisposeSetsTypeNull) {
    value_t v = make_i32_raw(vm, 42);
    value_dispose(vm, &v);
    EXPECT_EQ(v.type, nullptr);
    EXPECT_EQ(v.data, nullptr);
}

/* ---- error 机制 ---- */

TEST_F(ValueCore, MakeErrorIsTracked) {
    value_t err = value_make_error(vm, "test error");
    EXPECT_EQ(err.type, vm->type_error);
    EXPECT_TRUE(value_is_error(vm, &err));

    error_data_t *ed = (error_data_t *)err.data;
    EXPECT_STREQ(string_cstr(ed->message), "test error");
    EXPECT_EQ(ed->location, nullptr);

    /* error auto-track 到 current_scope，vm_destroy 释放 */
}

TEST_F(ValueCore, MakeErrorWithLocation) {
    value_t err = value_make_error_loc(vm, "boom", "file.clux:10");
    EXPECT_TRUE(value_is_error(vm, &err));

    error_data_t *ed = (error_data_t *)err.data;
    EXPECT_STREQ(string_cstr(ed->message), "boom");
    EXPECT_STREQ(string_cstr(ed->location), "file.clux:10");
}

TEST_F(ValueCore, NonErrorIsNotError) {
    value_t v = make_i32_raw(vm, 1);
    EXPECT_FALSE(value_is_error(vm, &v));
    value_dispose(vm, &v);

    value_t err = value_make_error(vm, "e");
    EXPECT_TRUE(value_is_error(vm, &err));
    EXPECT_FALSE(value_is_error(vm, &v)); /* v 已 dispose，type==NULL */
}

/* ---- 运算分派：error 短路 ---- */

TEST_F(ValueCore, ErrorShortCircuitsAdd) {
    value_t err = value_make_error(vm, "err");
    value_t v   = make_i32_raw(vm, 1);

    /* a 是 error → 直接返回 a */
    value_t r1 = value_add(vm, err, v);
    EXPECT_TRUE(value_is_error(vm, &r1));

    /* b 是 error → 直接返回 b */
    value_t r2 = value_add(vm, v, err);
    EXPECT_TRUE(value_is_error(vm, &r2));

    value_dispose(vm, &v);
    /* err, r1, r2 都 track 到 current_scope */
}

/* ---- 运算分派：类型不支持 → error ---- */

TEST_F(ValueCore, UnsupportedOperatorReturnsError) {
    value_t s = make_str_raw(vm, "hello");
    value_t s2 = make_str_raw(vm, "world");

    /* str 不支持 add */
    value_t r = value_add(vm, s, s2);
    EXPECT_TRUE(value_is_error(vm, &r));

    /* str 不支持 neg */
    value_t r2 = value_neg(vm, s);
    EXPECT_TRUE(value_is_error(vm, &r2));

    value_dispose(vm, &s);
    value_dispose(vm, &s2);
}

/* ---- 运算分派：正常路径 ---- */

TEST_F(ValueCore, IntAdditionDispatch) {
    value_t a = make_i32_raw(vm, 10);
    value_t b = make_i32_raw(vm, 32);

    value_t r = value_add(vm, a, b);
    EXPECT_EQ(r.type, vm->type_i32);
    EXPECT_EQ(read_sint(&r), 42);

    value_dispose(vm, &a);
    value_dispose(vm, &b);
    /* r 未 track（int_add 直接 value_make），需手动释放 */
    value_dispose(vm, &r);
}

TEST_F(ValueCore, IntComparisonReturnsBool) {
    value_t a = make_i32_raw(vm, 5);
    value_t b = make_i32_raw(vm, 10);

    value_t r = value_lt(vm, a, b);
    EXPECT_EQ(r.type, vm->type_bool);
    EXPECT_EQ(value_as(r, bool), true);
    value_dispose(vm, &r);

    value_dispose(vm, &a);
    value_dispose(vm, &b);
    value_dispose(vm, &r);
}

/* ---- value_call 分派 ---- */

TEST_F(ValueCore, CallNonCallableReturnsError) {
    value_t a = make_i32_raw(vm, 1);
    value_t r = value_call(vm, a, nullptr, 0);
    EXPECT_TRUE(value_is_error(vm, &r));

    value_dispose(vm, &a);
}

TEST_F(ValueCore, CallWithErrorArgPropagates) {
    value_t err = value_make_error(vm, "arg error");
    value_t a = make_i32_raw(vm, 1);

    value_t args[] = { err, a };
    value_t r = value_call(vm, a, args, 2);
    EXPECT_TRUE(value_is_error(vm, &r));

    value_dispose(vm, &a);
}

/* ---- 类型转换分派 ---- */

TEST_F(ValueCore, ImplicitCastSameTypeClones) {
    value_t v = make_i32_raw(vm, 42);
    value_t r = value_implicit_cast(vm, v, vm->type_i32);
    EXPECT_EQ(r.type, vm->type_i32);
    EXPECT_EQ(read_sint(&r), 42);
    /* clone 到 current_scope */

    value_dispose(vm, &v);
}

TEST_F(ValueCore, ImplicitCastUnsupportedReturnsError) {
    value_t v = make_i32_raw(vm, 42);
    /* int 没有 implicit_cast vtable 回调 */
    value_t r = value_implicit_cast(vm, v, vm->type_str);
    EXPECT_TRUE(value_is_error(vm, &r));

    value_dispose(vm, &v);
}

TEST_F(ValueCore, ExplicitCastUnsupportedReturnsError) {
    value_t v = make_i32_raw(vm, 42);
    value_t r = value_explicit_cast(vm, v, vm->type_str);
    EXPECT_TRUE(value_is_error(vm, &r));

    value_dispose(vm, &v);
}

TEST_F(ValueCore, ImplicitCastVoidReturnsError) {
    value_t v = { nullptr, nullptr };
    value_t r = value_implicit_cast(vm, v, vm->type_i32);
    EXPECT_TRUE(value_is_error(vm, &r));
}

TEST_F(ValueCore, ImplicitCastErrorPropagates) {
    value_t err = value_make_error(vm, "err");
    value_t r = value_implicit_cast(vm, err, vm->type_i32);
    EXPECT_TRUE(value_is_error(vm, &r));
}

/* ================================================================ */
/* 4. Scope 机制                                                     */
/* ================================================================ */

class ScopeMech : public ::testing::Test {
protected:
    allocator_t *alloc = nullptr;
    vm_t        *vm    = nullptr;

    void SetUp() override {
        alloc = create_allocator(test_alloc, test_free);
        vm    = vm_new(alloc);
    }
    void TearDown() override {
        vm_destroy(&vm);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
    }
};

/* ---- scope_define / scope_lookup ---- */

TEST_F(ScopeMech, DefineAndLookup) {
    value_t v = make_i32_raw(vm, 42);
    value_t *stored = scope_define(vm, vm->current_scope, "x", v);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->type, vm->type_i32);
    EXPECT_EQ(read_sint(stored), 42);

    /* lookup 能找到 */
    value_t *found = scope_lookup(vm->current_scope, STRSLICE_LIT("x"));
    EXPECT_EQ(found, stored);

    /* 释放 raw value */
    value_dispose(vm, &v);
}

TEST_F(ScopeMech, LookupNotFoundReturnsNull) {
    value_t *found = scope_lookup(vm->current_scope, STRSLICE_LIT("nonexistent"));
    EXPECT_EQ(found, nullptr);
}

TEST_F(ScopeMech, LookupTraversesParentChain) {
    vm_push_scope(vm);
    scope_t *child = vm->current_scope;

    /* 在 root_scope 定义变量 */
    value_t v = make_i32_raw(vm, 99);
    scope_define(vm, vm->root_scope, "parent_var", v);
    value_dispose(vm, &v);

    /* 在 child scope 能查找到 parent 的变量 */
    value_t *found = scope_lookup(child, STRSLICE_LIT("parent_var"));
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(read_sint(found), 99);

    vm_pop_scope(vm);
}

TEST_F(ScopeMech, DefineOverwritesPrevious) {
    value_t v1 = make_i32_raw(vm, 1);
    scope_define(vm, vm->current_scope, "x", v1);
    value_dispose(vm, &v1);

    value_t v2 = make_i32_raw(vm, 2);
    scope_define(vm, vm->current_scope, "x", v2);
    value_dispose(vm, &v2);

    value_t *found = scope_lookup(vm->current_scope, STRSLICE_LIT("x"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(read_sint(found), 2);
}

/* ---- scope_assign ---- */

TEST_F(ScopeMech, AssignUpdatesExisting) {
    value_t v = make_i32_raw(vm, 10);
    scope_define(vm, vm->current_scope, "x", v);
    value_dispose(vm, &v);

    value_t nv = make_i32_raw(vm, 99);
    bool ok = scope_assign(vm, vm->current_scope, STRSLICE_LIT("x"), nv);
    EXPECT_TRUE(ok);
    value_dispose(vm, &nv);

    value_t *found = scope_lookup(vm->current_scope, STRSLICE_LIT("x"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(read_sint(found), 99);
}

TEST_F(ScopeMech,AssignNonExistingReturnsFalse) {
    value_t v = make_i32_raw(vm, 1);
    bool ok = scope_assign(vm, vm->current_scope, STRSLICE_LIT("nope"), v);
    EXPECT_FALSE(ok);
    value_dispose(vm, &v);
}

TEST_F(ScopeMech, AssignTraversesParentChain) {
    value_t v = make_i32_raw(vm, 1);
    scope_define(vm, vm->root_scope, "p", v);
    value_dispose(vm, &v);

    vm_push_scope(vm);
    value_t nv = make_i32_raw(vm, 77);
    bool ok = scope_assign(vm, vm->current_scope, STRSLICE_LIT("p"), nv);
    EXPECT_TRUE(ok);
    value_dispose(vm, &nv);

    value_t *found = scope_lookup(vm->current_scope, STRSLICE_LIT("p"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(read_sint(found), 77);

    vm_pop_scope(vm);
}

/* ---- owned 生命周期 ---- */

TEST_F(ScopeMech, PopScopeDisposesOwnedValues) {
    vm_push_scope(vm);

    value_t v = make_i32_raw(vm, 42);
    value_t cloned = value_clone(vm, v); /* track 到 current_scope（push 出来的 scope） */
    EXPECT_EQ(read_sint(&cloned), 42);

    value_dispose(vm, &v);

    vm_pop_scope(vm); /* 应销毁 cloned，不泄漏 */

    /* allocator_live_count 在 TearDown 验证 */
}

TEST_F(ScopeMech, DefineInChildScopeDestroyedOnPop) {
    vm_push_scope(vm);

    value_t v = make_i32_raw(vm, 123);
    scope_define(vm, vm->current_scope, "child_var", v);
    value_dispose(vm, &v);

    /* 在 child scope 能查到 */
    value_t *found = scope_lookup(vm->current_scope, STRSLICE_LIT("child_var"));
    EXPECT_NE(found, nullptr);

    vm_pop_scope(vm);

    /* pop 后在 root scope 查不到 */
    found = scope_lookup(vm->root_scope, STRSLICE_LIT("child_var"));
    EXPECT_EQ(found, nullptr);
}

/* ---- 跨 scope clone ---- */

TEST_F(ScopeMech, CrossScopeCloneTrackedToTargetScope) {
    vm_push_scope(vm);
    scope_t *inner = vm->current_scope;

    /* 在 root_scope 定义变量 */
    value_t v = make_i32_raw(vm, 55);
    scope_define(vm, vm->root_scope, "root_var", v);
    value_dispose(vm, &v);

    /* 从 inner scope 查找到 root_var，clone 到 inner scope */
    value_t *found = scope_lookup(inner, STRSLICE_LIT("root_var"));
    ASSERT_NE(found, nullptr);

    /* 临时切换 current_scope 到 inner，clone 后 track 到 inner */
    vm->current_scope = inner;
    value_t cloned = value_clone(vm, *found);
    EXPECT_EQ(read_sint(&cloned), 55);

    vm_pop_scope(vm); /* 销毁 inner 及 cloned */

    /* root_var 仍存在 */
    found = scope_lookup(vm->root_scope, STRSLICE_LIT("root_var"));
    EXPECT_NE(found, nullptr);
}

/* ---- str 类型 owned 生命周期（dispose 释放 string_t） ---- */

TEST_F(ScopeMech, StrValueDisposedOnPopScope) {
    vm_push_scope(vm);

    value_t s = make_str_raw(vm, "hello world");
    value_t cloned = value_clone(vm, s); /* str_clone 深拷贝 string_t */
    (void)cloned;
    value_dispose(vm, &s);

    /* cloned track 到 current_scope，pop 时 str_dispose 释放 string_t */
    vm_pop_scope(vm);
}

/* ---- define NULL name ---- */

TEST_F(ScopeMech, DefineNullNameReturnsNull) {
    value_t v = make_i32_raw(vm, 1);
    EXPECT_EQ(scope_define(vm, vm->current_scope, nullptr, v), nullptr);
    value_dispose(vm, &v);
}

TEST_F(ScopeMech, DefineNullScopeReturnsNull) {
    value_t v = make_i32_raw(vm, 1);
    EXPECT_EQ(scope_define(vm, nullptr, "x", v), nullptr);
    value_dispose(vm, &v);
}

/* ---- scope_track 直接使用 ---- */

TEST_F(ScopeMech, TrackRegistersToOwned) {
    vm_push_scope(vm);

    /* scope_track 共享 data 指针（转移所有权），不能在 track 后 dispose raw value */
    value_t v = make_i32_raw(vm, 777);
    scope_track(vm, vm->current_scope, v);
    /* v.data 所有权已转移到 scope->owned，pop_scope 时统一释放 */

    vm_pop_scope(vm);
}
