#include <gtest/gtest.h>
#include "test_common.h"

extern "C" {
#include "vm/bcode.h"
#include "core/allocator.h"
#include "core/string.h"
}

/* ---- helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr)     { free(ptr); }

class BcodeTest : public ::testing::Test {
protected:
    allocator_t *alloc = nullptr;
    bytecode_t  *bc    = nullptr;

    void SetUp() override {
        alloc = create_allocator(test_alloc, test_free);
        bc    = bcode_new(alloc);
    }
    void TearDown() override {
        bcode_destroy(&bc);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
    }
};

/* ---- 生命周期 ---- */

TEST_F(BcodeTest, NewModuleIsEmpty) {
    EXPECT_EQ(bcode_str_count(bc), 0u);
    EXPECT_EQ(bcode_tell(bc), 0u);
}

/* ---- strtable intern 去重 ---- */

TEST_F(BcodeTest, StrInternDeduplicates) {
    size_t i1 = bcode_str_index(bc, STRSLICE_LIT("add"));
    size_t i2 = bcode_str_index(bc, STRSLICE_LIT("add"));
    size_t i3 = bcode_str_index(bc, STRSLICE_LIT("main"));

    EXPECT_EQ(i1, i2);
    EXPECT_NE(i1, i3);
    EXPECT_EQ(bcode_str_count(bc), 2u);
}

TEST_F(BcodeTest, StrInternEmptyString) {
    size_t i = bcode_str_index(bc, STRSLICE_EMPTY);
    EXPECT_EQ(i, 0u);
    EXPECT_EQ(bcode_str_count(bc), 1u);
    strslice_t s = bcode_str_at(bc, i);
    EXPECT_TRUE(strslice_is_empty(s));
}

TEST_F(BcodeTest, StrAtOutOfBoundsReturnsEmpty) {
    strslice_t s = bcode_str_at(bc, 99);
    EXPECT_TRUE(strslice_is_empty(s));
}

/* ---- writer/reader 往返：立即数 ---- */

TEST_F(BcodeTest, WriteReadImmediateRoundtrip) {
    bcode_write_op(bc, BCODE_PUSH_I32);
    bcode_write_i32(bc, -12345);
    bcode_write_u32(bc, 0xDEADBEEF);
    bcode_write_i64(bc, -9007199254740993LL);
    bcode_write_u64(bc, 0xFEDCBA9876543210ULL);
    bcode_write_f32(bc, 3.14f);
    bcode_write_f64(bc, 2.718281828459045);
    bcode_write_bool(bc, true);
    bcode_write_u8(bc, 0xAB);
    bcode_write_i8(bc, -5);
    bcode_write_u16(bc, 0xBEEF);
    bcode_write_i16(bc, -1234);

    size_t pc = 0;
    EXPECT_EQ(bcode_read_op(bc, &pc), BCODE_PUSH_I32);
    EXPECT_EQ(bcode_read_i32(bc, &pc), -12345);
    EXPECT_EQ(bcode_read_u32(bc, &pc), 0xDEADBEEFu);
    EXPECT_EQ(bcode_read_i64(bc, &pc), -9007199254740993LL);
    EXPECT_EQ(bcode_read_u64(bc, &pc), 0xFEDCBA9876543210ULL);
    EXPECT_FLOAT_EQ(bcode_read_f32(bc, &pc), 3.14f);
    EXPECT_DOUBLE_EQ(bcode_read_f64(bc, &pc), 2.718281828459045);
    EXPECT_TRUE(bcode_read_bool(bc, &pc));
    EXPECT_EQ(bcode_read_u8(bc, &pc), 0xAB);
    EXPECT_EQ(bcode_read_i8(bc, &pc), -5);
    EXPECT_EQ(bcode_read_u16(bc, &pc), 0xBEEF);
    EXPECT_EQ(bcode_read_i16(bc, &pc), -1234);
    EXPECT_EQ(pc, bcode_tell(bc)); /* 消费完整条 */
}

TEST_F(BcodeTest, LittleEndianLayout) {
    bcode_write_u32(bc, 0x01020304u);
    ASSERT_EQ(bcode_tell(bc), 4u);
    const uint8_t *d = bc->code.data;
    EXPECT_EQ(d[0], 0x04);
    EXPECT_EQ(d[1], 0x03);
    EXPECT_EQ(d[2], 0x02);
    EXPECT_EQ(d[3], 0x01);
}

/* ---- strtable 无感封装 ---- */

TEST_F(BcodeTest, WriteStrThenReadStr) {
    bcode_write_op(bc, BCODE_PUSH);
    bcode_write_str(bc, STRSLICE_LIT("count"));
    bcode_write_op(bc, BCODE_STORE);
    bcode_write_str(bc, STRSLICE_LIT("count"));

    /* 同字符串两次 write → strtable 去重为 1 条 */
    EXPECT_EQ(bcode_str_count(bc), 1u);

    size_t pc = 0;
    EXPECT_EQ(bcode_read_op(bc, &pc), BCODE_PUSH);
    strslice_t s1 = bcode_read_str(bc, &pc);
    EXPECT_EQ(s1.len, 5u);
    EXPECT_TRUE(strslice_eq(s1, STRSLICE_LIT("count")));

    EXPECT_EQ(bcode_read_op(bc, &pc), BCODE_STORE);
    strslice_t s2 = bcode_read_str(bc, &pc);
    EXPECT_TRUE(strslice_eq(s2, STRSLICE_LIT("count")));
    EXPECT_EQ(pc, bcode_tell(bc));
}

TEST_F(BcodeTest, WriteStrReturnsIndex) {
    size_t i1 = bcode_write_str(bc, STRSLICE_LIT("a"));
    size_t i2 = bcode_write_str(bc, STRSLICE_LIT("b"));
    EXPECT_EQ(i1, 0u);
    EXPECT_EQ(i2, 1u);
}

/* ---- 标签回填（JMP 目标 pc） ---- */

TEST_F(BcodeTest, LabelPatchRoundtrip) {
    /* 模拟：JMP 占位 → ...body... → 记录 L_end 位置 → 回填 */
    bcode_write_op(bc, BCODE_JMP);
    size_t patch_pos = bcode_tell(bc);
    bcode_write_u32(bc, 0); /* 占位 */

    bcode_write_op(bc, BCODE_PUSH_I32);
    bcode_write_i32(bc, 42);

    size_t label = bcode_tell(bc);
    bcode_patch_u32(bc, patch_pos, (uint32_t)label);

    size_t pc = 0;
    EXPECT_EQ(bcode_read_op(bc, &pc), BCODE_JMP);
    EXPECT_EQ(bcode_read_u32(bc, &pc), label);
    EXPECT_EQ(bcode_read_op(bc, &pc), BCODE_PUSH_I32);
    EXPECT_EQ(bcode_read_i32(bc, &pc), 42);
    EXPECT_EQ(pc, bcode_tell(bc));
}

TEST_F(BcodeTest, PatchAfterGrowth) {
    /* 回填点在缓冲多次扩容后依然正确（data 重新分配，offset 不变） */
    bcode_write_op(bc, BCODE_JMP);
    size_t patch_pos = bcode_tell(bc);
    bcode_write_u32(bc, 0);

    /* 写大量数据触发多次 buf 扩容 */
    for (int i = 0; i < 1000; i++) bcode_write_u64(bc, (uint64_t)i);

    size_t label = bcode_tell(bc);
    bcode_patch_u32(bc, patch_pos, (uint32_t)label);

    size_t pc = 0;
    EXPECT_EQ(bcode_read_op(bc, &pc), BCODE_JMP);
    EXPECT_EQ(bcode_read_u32(bc, &pc), label);
}
