#include "vm/bcode.h"
#include "vm/bcode_disasm.h"
#include "vm/bcode_asm.h"
#include "core/allocator.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

/* 读文件全部内容到 std::string（测试辅助）。 */
static std::string read_file(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/* 构造一个最小可反汇编的字节码模块，等价于示例程序：
 *   LOAD 0 (printf) ; PUSH_STRING 1 ("hello world\n") ; CALL 1 ; POP ; HALT */
static bytecode_t *make_sample_bc(allocator_t *alloc) {
    bytecode_t *bc = bcode_new(alloc);
    /* string 段 */
    (void)bcode_str_index(bc, strslice_from_cstr("printf"));
    (void)bcode_str_index(bc, strslice_from_cstr("hello world\n"));
    /* code 段 */
    bcode_write_op(bc, BCODE_LOAD);
    bcode_write_u32(bc, 0);
    bcode_write_op(bc, BCODE_PUSH_STR);
    bcode_write_u32(bc, 1);
    bcode_write_op(bc, BCODE_CALL);
    bcode_write_u32(bc, 1);
    bcode_write_op(bc, BCODE_POP);
    bcode_write_op(bc, BCODE_HALT);
    return bc;
}

TEST(BcodeDisasm, EmitsStringAndCodeSections) {
    allocator_t *alloc = create_allocator(malloc, free);
    ASSERT_NE(alloc, nullptr);

    bytecode_t *bc = make_sample_bc(alloc);
    const char *out = "bcode_disasm_test_sample.cxs";
    EXPECT_EQ(bcode_disasm(bc, out), 0);

    std::string text = read_file(out);

    /* 字符串内联为转义字面量（无独立字符串段） */
    EXPECT_NE(text.find("LOAD \"printf\""), std::string::npos);
    EXPECT_NE(text.find("PUSH_STRING \"hello world\\n\""), std::string::npos);
    EXPECT_NE(text.find("CALL 1"), std::string::npos);
    EXPECT_NE(text.find("POP"), std::string::npos);
    EXPECT_NE(text.find("HALT"), std::string::npos);

    /* 完整逐行匹配示例格式（大写助记符，字符串内联） */
    const std::string expected =
        "LOAD \"printf\"\n"
        "PUSH_STRING \"hello world\\n\"\n"
        "CALL 1\n"
        "POP\n"
        "HALT\n";
    EXPECT_EQ(text, expected);

    std::remove(out);
    bcode_destroy(&bc);
    delete_allocator(&alloc);
}

TEST(BcodeDisasm, EscapesQuotesBackslashAndControlChars) {
    allocator_t *alloc = create_allocator(malloc, free);
    ASSERT_NE(alloc, nullptr);

    bytecode_t *bc = bcode_new(alloc);
    /* 含双引号、反斜杠、制表符的字符串（末尾内嵌 NUL 亦应保留并转义） */
    const char tricky[] = {'"', '\\', '\t', 'a', '\0'};
    (void)bcode_str_index(bc, strslice_from_bytes(tricky, sizeof tricky));
    (void)bcode_str_index(bc, strslice_from_cstr("plain"));
    bcode_write_op(bc, BCODE_PUSH_STR);
    bcode_write_u32(bc, 0);
    bcode_write_op(bc, BCODE_HALT);

    const char *out = "bcode_disasm_test_escape.cxs";
    ASSERT_EQ(bcode_disasm(bc, out), 0);
    std::string text = read_file(out);

    /* " → \"  ;  \ → \\  ;  \t → \t  ;  内嵌 NUL → \x00（不截断） */
    EXPECT_NE(text.find("PUSH_STRING \"\\\"\\\\\\ta\\x00\""), std::string::npos);

    std::remove(out);
    bcode_destroy(&bc);
    delete_allocator(&alloc);
}

TEST(BcodeDisasm, DecodesVariadicAndArithmeticOpcodes) {
    allocator_t *alloc = create_allocator(malloc, free);
    ASSERT_NE(alloc, nullptr);

    bytecode_t *bc = bcode_new(alloc);
    (void)bcode_str_index(bc, strslice_from_cstr("x")); /* index 0 */
    bcode_write_op(bc, BCODE_PUSH_I32);
    bcode_write_i32(bc, -7);
    bcode_write_op(bc, BCODE_STORE);
    bcode_write_u32(bc, 0);
    bcode_write_op(bc, BCODE_ADD);
    bcode_write_op(bc, BCODE_JMP);
    bcode_write_u32(bc, 999);
    bcode_write_op(bc, BCODE_HALT);

    const char *out = "bcode_disasm_test_variants.cxs";
    ASSERT_EQ(bcode_disasm(bc, out), 0);
    std::string text = read_file(out);

    EXPECT_NE(text.find("PUSH_I32 -7"), std::string::npos);
    EXPECT_NE(text.find("STORE \"x\""), std::string::npos);
    EXPECT_NE(text.find("ADD"), std::string::npos);
    EXPECT_NE(text.find("JMP 999"), std::string::npos);

    std::remove(out);
    bcode_destroy(&bc);
    delete_allocator(&alloc);
}

TEST(BcodeDisasm, EmitsLabelsForJumpTargets) {
    allocator_t *alloc = create_allocator(malloc, free);
    ASSERT_NE(alloc, nullptr);

    /* 简单循环：JMP 回跳到 PUSH_I32 1 处（pc=16） */
    bytecode_t *bc = bcode_new(alloc);
    size_t idx_i = bcode_str_index(bc, strslice_from_cstr("i"));
    bcode_write_op(bc, BCODE_PUSH_I32); bcode_write_i32(bc, 0);            /* pc 0  */
    bcode_write_op(bc, BCODE_STORE);   bcode_write_u32(bc, (uint32_t)idx_i); /* pc 8  */
    bcode_write_op(bc, BCODE_PUSH_I32); bcode_write_i32(bc, 1);            /* pc 16 */
    bcode_write_op(bc, BCODE_ADD);                                    /* pc 24 */
    bcode_write_op(bc, BCODE_STORE);   bcode_write_u32(bc, (uint32_t)idx_i); /* pc 28 */
    bcode_write_op(bc, BCODE_JMP);     bcode_write_u32(bc, 16);         /* pc 36 */
    bcode_write_op(bc, BCODE_HALT);                                    /* pc 44 */

    const char *out = "bcode_disasm_test_labels.cxs";
    ASSERT_EQ(bcode_disasm(bc, out), 0);
    std::string text = read_file(out);

    /* 跳转目标 pc16 生成标签 L0 并内联为 [L0] 引用 */
    EXPECT_NE(text.find("L0:"), std::string::npos);
    EXPECT_NE(text.find("JMP [L0]"), std::string::npos);
    EXPECT_NE(text.find("L0:\nPUSH_I32 1"), std::string::npos);

    /* 往返：重新汇编后应再次得到 JMP [L0] */
    {
        bytecode_t *bc2 = nullptr;
        ASSERT_EQ(bcode_asm_from_file(alloc, out, &bc2), 0);
        char *t2 = bcode_disasm_mem(alloc, bc2, NULL);
        ASSERT_NE(t2, nullptr);
        EXPECT_NE(std::string(t2).find("JMP [L0]"), std::string::npos);
        allocator_free(alloc, (void **)&t2);
        bcode_destroy(&bc2);
    }

    std::remove(out);
    bcode_destroy(&bc);
    delete_allocator(&alloc);
}

TEST(BcodeDisasm, ReturnsErrorOnBadPath) {
    allocator_t *alloc = create_allocator(malloc, free);
    ASSERT_NE(alloc, nullptr);
    bytecode_t *bc = make_sample_bc(alloc);
    /* 写到一个非法路径（目录不存在）应失败 */
    EXPECT_NE(bcode_disasm(bc, "/this/path/should/not/exist.cxs"), 0);
    bcode_destroy(&bc);
    delete_allocator(&alloc);
}
