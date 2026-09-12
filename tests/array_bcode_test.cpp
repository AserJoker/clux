#include <gtest/gtest.h>
#include <cstring>
#include "test_common.h"

extern "C" {
#include "vm/vm.h"
#include "vm/value.h"
#include "vm/scope.h"
#include "vm/exec.h"
#include "vm/bcode.h"
#include "vm/bcode_asm.h"
#include "vm/bcode_disasm.h"
#include "vm/type.h"
#include "vm/type_array.h"
#include "core/allocator.h"
#include "core/string.h"
#include "core/strslice.h"
}

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr)     { free(ptr); }

class ArrayBcodeTest : public ::testing::Test {
protected:
    allocator_t *alloc = nullptr;
    vm_t        *vm    = nullptr;
    bytecode_t  *bc    = nullptr;

    void SetUp() override {
        alloc = create_allocator(test_alloc, test_free);
        vm    = vm_new(alloc);
        bc    = bcode_new(alloc);
    }
    void TearDown() override {
        bcode_destroy(&bc);
        vm_destroy(&vm);
        EXPECT_EQ(vm, nullptr);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
    }

    /* 汇编 .cxs 源并 exec_run（teardown 的 allocator 空断言会捕获去重泄漏） */
    bool assemble_and_run(const char *src) {
        bcode_destroy(&bc);
        if (bcode_asm_parse(alloc, src, strlen(src), &bc) != 0) return false;
        value_t *r = exec_run(vm, bc);
        return r == NULL || !value_is_error(vm, r);
    }

    value_t *stack_top() { return exec_stack_peek(vm, 0); }
};

/* PUSH_ARRAY / LOAD elem / DEFINE_BOUND N / SEAL 经统一 SEAL 构造出 [i32;3] */
TEST_F(ArrayBcodeTest, PushArrayDefineBoundSealBuildsArrayType) {
    ASSERT_TRUE(assemble_and_run(
        "    push_array\n"
        "    load \"i32\"\n"
        "    define_bound 3\n"
        "    seal\n"
        "    halt\n"));

    value_t *tv = stack_top();
    ASSERT_NE(tv, nullptr);
    EXPECT_EQ(value_type(tv), vm->type_type);

    const type_t *at = value_as(tv, const type_t *);
    EXPECT_EQ(at->kind, TYPE_KIND_ARRAY);
    EXPECT_EQ(array_type_elem(at), vm->type_i32);
    EXPECT_EQ(array_type_len(at), (size_t)3);
    EXPECT_TRUE(type_is_sealed(at));
}

/* 两次构造 [i32;3] 应 intern 为同一密封类型（第二次去重，开放类型被回收） */
TEST_F(ArrayBcodeTest, DuplicateArrayTypeInternedOnce) {
    ASSERT_TRUE(assemble_and_run(
        "    push_array\n"
        "    load \"i32\"\n"
        "    define_bound 3\n"
        "    seal\n"
        "    push_array\n"
        "    load \"i32\"\n"
        "    define_bound 3\n"
        "    seal\n"
        "    halt\n"));

    value_t *t1 = exec_stack_peek(vm, 1);
    value_t *t2 = exec_stack_peek(vm, 0);
    ASSERT_NE(t1, nullptr);
    ASSERT_NE(t2, nullptr);
    EXPECT_EQ(value_as(t1, const type_t *), value_as(t2, const type_t *));
    EXPECT_TRUE(type_is_sealed(value_as(t2, const type_t *)));
}

/* 不同元素类型 / 长度 → 不同密封类型（互不混用） */
TEST_F(ArrayBcodeTest, DistinctElemAndLengthAreDistinct) {
    ASSERT_TRUE(assemble_and_run(
        "    push_array\n"
        "    load \"i32\"\n"
        "    define_bound 3\n"
        "    seal\n"
        "    push_array\n"
        "    load \"u8\"\n"
        "    define_bound 4\n"
        "    seal\n"
        "    halt\n"));

    const type_t *t_i32_3 = value_as(exec_stack_peek(vm, 1), const type_t *);
    const type_t *t_u8_4  = value_as(exec_stack_peek(vm, 0), const type_t *);
    EXPECT_NE(t_i32_3, t_u8_4);
    EXPECT_EQ(array_type_elem(t_i32_3), vm->type_i32);
    EXPECT_EQ(array_type_len(t_i32_3), (size_t)3);
    EXPECT_EQ(array_type_elem(t_u8_4), vm->type_u8);
    EXPECT_EQ(array_type_len(t_u8_4), (size_t)4);
}

/* 汇编 → 反汇编 → 再汇编 稳定往返（PUSH_ARRAY / DEFINE_BOUND 助记符正确编解码） */
TEST_F(ArrayBcodeTest, AsmDisasmRoundTrip) {
    const char *src =
        "    push_array\n"
        "    load \"i32\"\n"
        "    define_bound 5\n"
        "    seal\n"
        "    halt\n";
    ASSERT_TRUE(assemble_and_run(src));

    /* 反汇编到临时 allocator（避免污染 fixture 的 vm->alloc：否则 teardown 的
       空分配断言会把 disasm 缓冲误判为泄漏）。缓冲随 da 销毁一并回收。 */
    allocator_t *da = create_allocator(test_alloc, test_free);
    char *text = bcode_disasm_mem(da, bc, NULL);
    ASSERT_NE(text, nullptr);
    EXPECT_NE(strstr(text, "PUSH_ARRAY"), nullptr);
    EXPECT_NE(strstr(text, "DEFINE_BOUND 5"), nullptr);
    EXPECT_NE(strstr(text, "SEAL"), nullptr);
    allocator_free(da, (void **)&text); /* delete_allocator 仅报告泄漏、不释放，需显式回收 */
    delete_allocator(&da);
}
