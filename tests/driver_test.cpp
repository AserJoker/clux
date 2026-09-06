#include <gtest/gtest.h>
#include <cstdio>
#include <string>
#include <vector>

extern "C" {
#include "core/allocator.h"
#include "core/vec.h"
#include "driver/driver.h"
#include "parser/lexer.h"
}

#include "test_common.h"

/* ---- helpers ---- */

static std::string write_temp_file(const char *content) {
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *fp = fopen(path.c_str(), "wb");
  fwrite(content, 1, strlen(content), fp);
  fclose(fp);
  return path;
}

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

  /* The buffer is allocator-managed; release it before teardown. */
  allocator_free(alloc, (void **)&data);
  EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc);
  remove(path.c_str());
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
  remove(path.c_str());
}

/* ---- Stage ①+②+③: run entry point ---- */

TEST(Driver, RunFileValidReturnsZero) {
  std::string path = write_temp_file("func main() { return 0; }\n");
  EXPECT_EQ(driver_run_file(path.c_str()), 0);
  remove(path.c_str());
}

TEST(Driver, RunFileMissingReturnsOne) {
  EXPECT_EQ(driver_run_file("no/such/file.cx"), 1);
}

TEST(Driver, RunFileLexErrorReturnsOne) {
  std::string path = write_temp_file("@ not a token\n");
  EXPECT_EQ(driver_run_file(path.c_str()), 1);
  remove(path.c_str());
}
