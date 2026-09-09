#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include "core/allocator.h"
#include "core/vec.h"
#include "driver/driver.h"
#include "parser/lexer.h"
}

#include "test_common.h"

namespace {

std::string write_temp_file(const std::string &content) {
  auto path = std::filesystem::temp_directory_path() / "clux_test_XXXXXX";
  auto path_str = path.string();
  /* mkstemps is not available on Windows; use a simple unique name. */
  static int counter = 0;
  path_str += std::to_string(counter++);
  FILE *fp = fopen(path_str.c_str(), "wb");
  fwrite(content.data(), 1, content.size(), fp);
  fclose(fp);
  return path_str;
}

} // namespace

/* ---- Stage ①: load source ---- */

TEST(Driver, LoadSource) {
  allocator_t *alloc = create_allocator(malloc, free);
  std::string path = write_temp_file("func main() {}");

  const char *data = NULL;
  size_t len = 0;
  EXPECT_EQ(driver_load_source(alloc, path.c_str(), &data, &len), 0);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(len, 14u);
  EXPECT_EQ(std::string(data, len), "func main() {}");

  allocator_free(alloc, (void **)&data);
  EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
  std::remove(path.c_str());
}

TEST(Driver, LoadSourceMissingFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = NULL;
  size_t len = 0;
  EXPECT_EQ(driver_load_source(alloc, "no/such/file.cx", &data, &len), -1);
  EXPECT_EQ(data, nullptr);
  EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
}

/* ---- Stage ②: lex into a pool ---- */

TEST(Driver, LexFileProducesTokens) {
  allocator_t *alloc = create_allocator(malloc, free);
  std::string path = write_temp_file("func main() {\n  return 0;\n}\n");

  vec_t *pool = NULL;
  ASSERT_EQ(driver_lex_file(alloc, path.c_str(), &pool), 0);
  ASSERT_NE(pool, nullptr);
  ASSERT_GT(vec_len(pool), 0u);

  /* Collect kinds in order. */
  std::vector<token_kind_t> kinds;
  for (size_t i = 0; i < vec_len(pool); i++) {
    const token_t *t = (const token_t *)vec_get(pool, i);
    kinds.push_back(token_get_kind(t));
  }

  /* Expect the leading keyword "func" and an EOF terminator. */
  EXPECT_EQ(kinds.front(), TOKEN_TYPE_KEYWORD);
  EXPECT_EQ(kinds.back(), TOKEN_TYPE_EOF);

  vec_free(alloc, &pool);
  EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
  std::remove(path.c_str());
}

/* ---- Stage ①+②+③: run entry point ---- */

TEST(Driver, RunFileValidReturnsZero) {
  /* 合法程序：词法 + 语法 + 语义全通过。main 无返回类型（void），
     函数体不含 return 值，sema 无诊断。 */
  std::string path = write_temp_file("func main() { var x = 1; }\n");
  EXPECT_EQ(driver_run_file(path.c_str()), 0);
  std::remove(path.c_str());
}

TEST(Driver, RunFileMissingReturnsOne) {
  EXPECT_EQ(driver_run_file("no/such/file.cx"), 1);
}

TEST(Driver, RunFileLexErrorReturnsOne) {
  std::string path = write_temp_file("@ not a token\n");
  EXPECT_EQ(driver_run_file(path.c_str()), 1);
  std::remove(path.c_str());
}

/* ---- Stage ④: sema ---- */

TEST(Driver, RunFileSemaErrorReturnsOne) {
  /* 语义错误：实参类型不匹配（str → i32），sema 应快速失败返回 1 */
  std::string path =
      write_temp_file("func foo(a:i32) { } func main() { foo(\"s\"); }\n");
  EXPECT_EQ(driver_run_file(path.c_str()), 1);
  std::remove(path.c_str());
}

TEST(Driver, RunFileValidSemaPassesReturnsZero) {
  /* 合法程序：带返回类型（:i32）+ 函数调用 + 变量推断，sema 全通过 */
  std::string path = write_temp_file(
      "func add(a:i32, b:i32):i32 { return a + b; }"
      "func main() { var x = add(1, 2); }\n");
  EXPECT_EQ(driver_run_file(path.c_str()), 0);
  std::remove(path.c_str());
}
