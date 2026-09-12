#include "parser/fmt.h"
#include "parser/lexer.h"

#include "core/allocator.h"
#include "core/stream.h"
#include "core/vec.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "test_common.h"

namespace {

/* 对源码文本执行格式化，返回结果字符串（alloc 缓冲随 delete_allocator 回收）。 */
std::string fmt(const char *src) {
    allocator_t *a = create_allocator(malloc, free);
    EXPECT_NE(a, nullptr);

    stream_source_t source =
        stream_source_mem(a, src, std::strlen(src), /*owns_data=*/false);
    istream_t *stream = istream_open(a, source);
    EXPECT_NE(stream, nullptr);

    lexer_t *lexer = lexer_create(a, stream, "<test>");
    EXPECT_NE(lexer, nullptr);

    vec_t *pool = vec_new(a, /*owns_element=*/true);
    for (;;) {
        token_t *t = lexer_next(lexer);
        if (!t) break;
        vec_push(pool, a, t);
        if (token_get_kind(t) == TOKEN_TYPE_EOF) break;
    }

    char *out = fmt_format(a, pool, nullptr);
    std::string result = out ? out : "";

    /* fmt_format 的缓冲由 alloc 分配，需显式释放（不随 delete_allocator 回收） */
    if (out) allocator_free(a, (void **)&out);
    vec_free(a, &pool);
    lexer_close(&lexer);
    EXPECT_ALLOCATOR_EMPTY_DELETE(&a);
    return result;
}

} // namespace

/* 基本排版：'{' 跟随前行、'{' 后换行、'}' 单独一行、4 空格缩进。 */
TEST(Fmt, BasicBracesAndIndent) {
    std::string out = fmt("func main():i32{\nreturn 0;\n}\n");
    EXPECT_EQ(out,
              "func main():i32 {\n"
              "    return 0;\n"
              "}\n");
}

/* 空块紧凑：`{}` 写在同一行。 */
TEST(Fmt, EmptyBlockIsCompact) {
    std::string out = fmt("func f():void{}\n");
    EXPECT_EQ(out, "func f():void {}\n");
}

/* 分号后必须换行（非括号内）。 */
TEST(Fmt, SemicolonForcesNewline) {
    std::string out = fmt("func main():void{var a:i32=1;var b:i32=2;}\n");
    EXPECT_EQ(out,
              "func main():void {\n"
              "    var a:i32 = 1;\n"
              "    var b:i32 = 2;\n"
              "}\n");
}

/* for 头部括号内的分号不换行。 */
TEST(Fmt, SemicolonInsideParensStaysInline) {
    std::string out = fmt("func main():void{for(var i:i32=0;i<5;i=i+1){}}\n");
    EXPECT_EQ(out,
              "func main():void {\n"
              "    for(var i:i32 = 0; i < 5; i = i + 1) {}\n"
              "}\n");
}

/* '}' 后跟 else 时同行：`} else {`（非空块场景）。 */
TEST(Fmt, ElseFollowsCloseBrace) {
    std::string out = fmt(
        "func f():void{\n"
        "if(1>0){\n"
        "return;\n"
        "}else{\n"
        "return;\n"
        "}\n"
        "}\n");
    EXPECT_EQ(out,
              "func f():void {\n"
              "    if(1 > 0) {\n"
              "        return;\n"
              "    } else {\n"
              "        return;\n"
              "    }\n"
              "}\n");
}

/* 两个空块：各自紧凑（空块规则优先），成为 `if() {} else {}`。 */
TEST(Fmt, EmptyBlocksWithElseStayCompact) {
    std::string out = fmt("func f():void{if(1>0){}else{}}\n");
    EXPECT_EQ(out,
              "func f():void {\n"
              "    if(1 > 0) {} else {}\n"
              "}\n");
}

/* TAB 缩进被替换为 4 空格。 */
TEST(Fmt, TabBecomesFourSpaces) {
    std::string out = fmt("func f():void{\n\treturn;\n}\n");
    EXPECT_EQ(out,
              "func f():void {\n"
              "    return;\n"
              "}\n");
}

/* 注释原样保留：独立行注释与行内注释。 */
TEST(Fmt, CommentsPreserved) {
    std::string out = fmt("// lead\nfunc f():void{\nvar a:i32=1; // inline\n}\n");
    EXPECT_EQ(out,
              "// lead\n"
              "func f():void {\n"
              "    var a:i32 = 1; // inline\n"
              "}\n");
}

/* 空行保留（用户的分组意图）。 */
TEST(Fmt, BlankLinePreserved) {
    std::string out = fmt("func f():void{\nvar a:i32=1;\n\nvar b:i32=2;\n}\n");
    EXPECT_EQ(out,
              "func f():void {\n"
              "    var a:i32 = 1;\n"
              "\n"
              "    var b:i32 = 2;\n"
              "}\n");
}

/* 幂等性：格式化结果再格式化保持不变。 */
TEST(Fmt, Idempotent) {
    const char *src =
        "func main():i32{\n"
        "var sum:i32=0;var i:i32=0;\n"
        "while(i<5){sum=sum+i;i=i+1;\n"
        "}\n"
        "for(var j:i32=0;j<5;j=j+1){if(j==3){continue;}\n"
        "sum=sum+j;}\n"
        "if(sum>100){printf(\"big\\n\");}else{printf(\"small\\n\");}\n"
        "return sum;\n"
        "}\n";
    std::string once = fmt(src);
    std::string twice = fmt(once.c_str());
    EXPECT_EQ(once, twice);
}

/* 嵌套块缩进逐层 +4。 */
TEST(Fmt, NestedIndent) {
    std::string out = fmt(
        "func f():void{\n"
        "while(1>0){\n"
        "if(2>0){\n"
        "return;\n"
        "}\n"
        "}\n"
        "}\n");
    EXPECT_EQ(out,
              "func f():void {\n"
              "    while(1 > 0) {\n"
              "        if(2 > 0) {\n"
              "            return;\n"
              "        }\n"
              "    }\n"
              "}\n");
}

/* 输出总以换行结束（非空输入）。 */
TEST(Fmt, EndsWithNewline) {
    std::string out = fmt("func f():void{}");
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.back(), '\n');
}

/* 空输入不崩溃。 */
TEST(Fmt, EmptyInput) {
    std::string out = fmt("");
    EXPECT_TRUE(out.empty());
}
