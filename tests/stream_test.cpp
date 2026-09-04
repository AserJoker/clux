#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "core/stream.h"
#include "core/allocator.h"
}

static allocator_t *g_alloc = nullptr;

class StreamTest : public ::testing::Test {
protected:
  void SetUp() override {
    g_alloc = create_allocator(malloc, free);
  }
  void TearDown() override {
    delete_allocator(&g_alloc);
  }
};

/* ---- istream construction ---- */

TEST(IStreamTest, OpenAndClose) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "Hello";
  stream_source_t src = stream_source_mem(alloc, data, 5, false);
  istream_t *s = istream_open(alloc, src);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(istream_size(s), 5u);
  EXPECT_FALSE(istream_at_end(s));
  istream_close(&s);
  EXPECT_EQ(s, nullptr);
  delete_allocator(&alloc);
}

TEST(IStreamTest, NullArgs) {
  EXPECT_EQ(istream_open(nullptr, stream_source_mem(g_alloc, "x", 1, false)), nullptr);
  stream_source_t empty_src = {0};
  EXPECT_EQ(istream_open(g_alloc, empty_src), nullptr);
  EXPECT_EQ(istream_open(g_alloc, stream_source_mem(nullptr, "x", 1, false)), nullptr);
  EXPECT_EQ(istream_open(g_alloc, stream_source_mem(g_alloc, nullptr, 1, false)), nullptr);
  istream_close(nullptr);
  istream_close(nullptr);
}

/* ---- istream read_cp ---- */

TEST(IStreamTest, ReadAscii) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "ABC";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 3, false));

  EXPECT_EQ(istream_read_cp(s), 0x41);
  EXPECT_EQ(istream_read_cp(s), 0x42);
  EXPECT_EQ(istream_read_cp(s), 0x43);
  EXPECT_EQ(istream_read_cp(s), -1);
  EXPECT_TRUE(istream_at_end(s));

  istream_close(&s);
  delete_allocator(&alloc);
}

TEST(IStreamTest, ReadUtf8Multibyte) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "\xE4\xBD\xA0\xE5\xA5\xBD";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 6, false));

  EXPECT_EQ(istream_read_cp(s), 0x4F60); /* 你 */
  EXPECT_EQ(istream_read_cp(s), 0x597D); /* 好 */
  EXPECT_EQ(istream_read_cp(s), -1);

  istream_close(&s);
  delete_allocator(&alloc);
}

TEST(IStreamTest, PeekDoesNotAdvance) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "AB";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 2, false));

  EXPECT_EQ(istream_peek_cp(s), 0x41);
  EXPECT_EQ(istream_peek_cp(s), 0x41);
  EXPECT_EQ(istream_read_cp(s), 0x41);
  EXPECT_EQ(istream_peek_cp(s), 0x42);

  istream_close(&s);
  delete_allocator(&alloc);
}

/* ---- istream position tracking ---- */

TEST(IStreamTest, TellBasicPosition) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "AB";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 2, false));

  stream_pos_t pos = istream_tell(s);
  EXPECT_EQ(pos.byte_offset, 0u);
  EXPECT_EQ(pos.line, 1u);
  EXPECT_EQ(pos.col, 1u);
  EXPECT_EQ(pos.cluster_col, 1u);

  istream_read_cp(s); /* 'A' */
  pos = istream_tell(s);
  EXPECT_EQ(pos.byte_offset, 1u);
  EXPECT_EQ(pos.line, 1u);
  EXPECT_EQ(pos.col, 2u);
  EXPECT_EQ(pos.cluster_col, 2u);

  istream_close(&s);
  delete_allocator(&alloc);
}

TEST(IStreamTest, LineTrackingLF) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "A\nB";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 3, false));

  istream_read_cp(s); /* 'A' */
  EXPECT_EQ(istream_tell(s).line, 1u);
  EXPECT_EQ(istream_tell(s).col, 2u);

  istream_read_cp(s); /* '\n' */
  EXPECT_EQ(istream_tell(s).line, 2u);
  EXPECT_EQ(istream_tell(s).col, 1u);
  EXPECT_EQ(istream_tell(s).cluster_col, 1u);

  istream_read_cp(s); /* 'B' */
  EXPECT_EQ(istream_tell(s).line, 2u);
  EXPECT_EQ(istream_tell(s).col, 2u);

  istream_close(&s);
  delete_allocator(&alloc);
}

TEST(IStreamTest, LineTrackingCRLF) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "A\r\nB";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 4, false));

  istream_read_cp(s); /* 'A' */
  istream_read_cp(s); /* '\r' — CR+LF treated as one line break */
  EXPECT_EQ(istream_tell(s).line, 2u);
  EXPECT_EQ(istream_tell(s).col, 1u);

  istream_read_cp(s); /* 'B' */
  EXPECT_EQ(istream_tell(s).line, 2u);
  EXPECT_EQ(istream_tell(s).col, 2u);

  istream_close(&s);
  delete_allocator(&alloc);
}

TEST(IStreamTest, ClusterColEmoji) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "\xF0\x9F\x91\x8B\xF0\x9F\x8F\xBF";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 8, false));

  istream_read_cp(s); /* U+1F44B waving hand */
  EXPECT_EQ(istream_tell(s).col, 2u);       /* cp col incremented */
  EXPECT_EQ(istream_tell(s).cluster_col, 2u); /* cluster col incremented (base) */

  istream_read_cp(s); /* U+1F3FF dark skin tone (grapheme extend) */
  EXPECT_EQ(istream_tell(s).col, 3u);       /* cp col still increments */
  EXPECT_EQ(istream_tell(s).cluster_col, 2u); /* cluster col unchanged (extend) */

  istream_close(&s);
  delete_allocator(&alloc);
}

/* ---- istream seek ---- */

TEST(IStreamTest, SeekToStart) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "A\nB\nC";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 5, false));

  istream_read_cp(s);
  istream_read_cp(s);
  istream_read_cp(s);

  istream_seek(s, 0);
  stream_pos_t pos = istream_tell(s);
  EXPECT_EQ(pos.byte_offset, 0u);
  EXPECT_EQ(pos.line, 1u);
  EXPECT_EQ(pos.col, 1u);
  EXPECT_EQ(pos.cluster_col, 1u);

  istream_close(&s);
  delete_allocator(&alloc);
}

TEST(IStreamTest, SeekToMiddleLine) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "A\nBC";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 4, false));

  /* Seek to 'C' at byte offset 3 */
  istream_seek(s, 3);
  stream_pos_t pos = istream_tell(s);
  EXPECT_EQ(pos.byte_offset, 3u);
  EXPECT_EQ(pos.line, 2u);
  EXPECT_EQ(pos.col, 2u); /* 'C' is 2nd codepoint on line 2 */

  EXPECT_EQ(istream_read_cp(s), 0x43); /* 'C' */

  istream_close(&s);
  delete_allocator(&alloc);
}

/* ---- istream scanf ---- */

TEST(IStreamTest, ScanfInt) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "42 hello";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 8, false));

  int val = 0;
  int result = istream_scanf(s, "%d", &val);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(val, 42);

  istream_close(&s);
  delete_allocator(&alloc);
}

/* ---- istream remaining / at_end ---- */

TEST(IStreamTest, RemainingAndAtEnd) {
  allocator_t *alloc = create_allocator(malloc, free);
  const char *data = "AB";
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 2, false));

  EXPECT_EQ(istream_remaining(s), 2u);
  EXPECT_FALSE(istream_at_end(s));

  istream_read_cp(s);
  EXPECT_EQ(istream_remaining(s), 1u);

  istream_read_cp(s);
  EXPECT_EQ(istream_remaining(s), 0u);
  EXPECT_TRUE(istream_at_end(s));

  istream_close(&s);
  delete_allocator(&alloc);
}

/* ---- istream owns data ---- */

TEST(IStreamTest, OwnsData) {
  allocator_t *alloc = create_allocator(malloc, free);
  char *data = (char *)allocator_new_ex(alloc, "test_str", 1,
                                         default_move, default_clone,
                                         NULL, 4);
  memcpy(data, "ABC", 4);

  /* Source takes ownership of data */
  istream_t *s = istream_open(alloc, stream_source_mem(alloc, data, 3, true));
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(istream_read_cp(s), 0x41);

  istream_close(&s);
  /* data should have been freed by source close */
  delete_allocator(&alloc);
}

/* ================================================================ */
/* ostream_t tests                                                   */
/* ================================================================ */

TEST(OStreamTest, OpenAndClose) {
  allocator_t *alloc = create_allocator(malloc, free);
  stream_sink_t sink = stream_sink_mem(alloc);
  ostream_t *s = ostream_open(alloc, sink);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(ostream_size(s), 0u);
  ostream_close(&s);
  EXPECT_EQ(s, nullptr);
  delete_allocator(&alloc);
}

TEST(OStreamTest, WriteCpAscii) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  ostream_write_cp(s, 'A');
  ostream_write_cp(s, 'B');
  ostream_write_cp(s, 'C');

  EXPECT_EQ(ostream_size(s), 3u);
  EXPECT_STREQ(ostream_data(s), "ABC");

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, WriteCpUtf8) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  ostream_write_cp(s, 0x4F60); /* 你 */
  ostream_write_cp(s, 0x597D); /* 好 */

  EXPECT_EQ(ostream_size(s), 6u);

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, WriteRaw) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  ostream_write(s, "Hello", 5);
  EXPECT_EQ(ostream_size(s), 5u);
  EXPECT_STREQ(ostream_data(s), "Hello");

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, Printf) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  int n = ostream_printf(s, "%d + %d = %d", 1, 2, 3);
  EXPECT_GT(n, 0);
  EXPECT_STREQ(ostream_data(s), "1 + 2 = 3");

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, TellPosition) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  stream_pos_t pos = ostream_tell(s);
  EXPECT_EQ(pos.byte_offset, 0u);
  EXPECT_EQ(pos.line, 1u);
  EXPECT_EQ(pos.col, 1u);
  EXPECT_EQ(pos.cluster_col, 1u);

  ostream_write_cp(s, 'A');
  pos = ostream_tell(s);
  EXPECT_EQ(pos.byte_offset, 1u);
  EXPECT_EQ(pos.col, 2u);
  EXPECT_EQ(pos.cluster_col, 2u);

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, LineTracking) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  ostream_write_cp(s, 'A');
  ostream_write_cp(s, '\n');
  EXPECT_EQ(ostream_tell(s).line, 2u);
  EXPECT_EQ(ostream_tell(s).col, 1u);

  ostream_write_cp(s, 'B');
  EXPECT_EQ(ostream_tell(s).line, 2u);
  EXPECT_EQ(ostream_tell(s).col, 2u);

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, ClusterColEmoji) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  /* Write waving hand + skin tone = 1 grapheme cluster */
  ostream_write_cp(s, 0x1F44B);
  EXPECT_EQ(ostream_tell(s).col, 2u);
  EXPECT_EQ(ostream_tell(s).cluster_col, 2u);

  ostream_write_cp(s, 0x1F3FF); /* skin tone (extend) */
  EXPECT_EQ(ostream_tell(s).col, 3u);       /* cp col increments */
  EXPECT_EQ(ostream_tell(s).cluster_col, 2u); /* cluster unchanged */

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, Reset) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  ostream_write(s, "Hello\nWorld", 11);
  EXPECT_EQ(ostream_tell(s).line, 2u);

  ostream_reset(s);
  EXPECT_EQ(ostream_size(s), 0u);
  EXPECT_EQ(ostream_tell(s).line, 1u);
  EXPECT_EQ(ostream_tell(s).col, 1u);

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, GrowBuffer) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  /* Write enough to trigger multiple growth cycles */
  for (int i = 0; i < 200; i++) {
    ostream_write_cp(s, 'X');
  }
  EXPECT_EQ(ostream_size(s), 200u);
  EXPECT_GE(ostream_tell(s).col, 201u);

  ostream_close(&s);
  delete_allocator(&alloc);
}

TEST(OStreamTest, PrintfLineTracking) {
  allocator_t *alloc = create_allocator(malloc, free);
  ostream_t *s = ostream_open(alloc, stream_sink_mem(alloc));

  ostream_printf(s, "Line1\nLine2\n");
  EXPECT_EQ(ostream_tell(s).line, 3u);
  EXPECT_EQ(ostream_tell(s).col, 1u);

  ostream_close(&s);
  delete_allocator(&alloc);
}

/* ---- Null safety ---- */

TEST(IStreamTest, NullSafety) {
  EXPECT_EQ(istream_read_cp(nullptr), -1);
  EXPECT_EQ(istream_peek_cp(nullptr), -1);
  EXPECT_EQ(istream_tell(nullptr).byte_offset, 0u);
  EXPECT_EQ(istream_size(nullptr), 0u);
  EXPECT_EQ(istream_remaining(nullptr), 0u);
  EXPECT_TRUE(istream_at_end(nullptr));
}

TEST(OStreamTest, NullSafety) {
  ostream_write_cp(nullptr, 'A');
  ostream_write(nullptr, "x", 1);
  ostream_printf(nullptr, "x");
  EXPECT_EQ(ostream_tell(nullptr).byte_offset, 0u);
  EXPECT_EQ(ostream_size(nullptr), 0u);
  EXPECT_EQ(ostream_data(nullptr), nullptr);
}

/* ================================================================ */
/* file source/sink tests                                            */
/* ================================================================ */

#include <cstdio>

/* ---- file source ---- */

TEST(FileSourceTest, ReadFileByPath) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("Hello", 1, 5, wfp);
  fclose(wfp);

  stream_source_t src = stream_source_file(alloc, path.c_str());
  ASSERT_NE(src.ctx, nullptr);

  istream_t *s = istream_open(alloc, src);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(istream_size(s), 5u);

  EXPECT_EQ(istream_read_cp(s), 'H');
  EXPECT_EQ(istream_read_cp(s), 'e');
  EXPECT_EQ(istream_read_cp(s), 'l');
  EXPECT_EQ(istream_read_cp(s), 'l');
  EXPECT_EQ(istream_read_cp(s), 'o');
  EXPECT_EQ(istream_read_cp(s), -1);
  EXPECT_TRUE(istream_at_end(s));

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSourceTest, ReadFileByFp) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("AB", 1, 2, wfp);
  fclose(wfp);

  FILE *fp = fopen(path.c_str(), "rb");
  ASSERT_NE(fp, nullptr);

  stream_source_t src = stream_source_file_fp(alloc, fp, true);
  ASSERT_NE(src.ctx, nullptr);

  istream_t *s = istream_open(alloc, src);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(istream_read_cp(s), 'A');
  EXPECT_EQ(istream_read_cp(s), 'B');

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSourceTest, SeekInFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("A\nB\nC", 1, 5, wfp);
  fclose(wfp);

  istream_t *s = istream_open(alloc, stream_source_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);

  /* Read past first line */
  istream_read_cp(s); /* 'A' */
  istream_read_cp(s); /* '\n' */

  /* Seek back to start */
  istream_seek(s, 0);
  stream_pos_t pos = istream_tell(s);
  EXPECT_EQ(pos.byte_offset, 0u);
  EXPECT_EQ(pos.line, 1u);
  EXPECT_EQ(pos.col, 1u);

  /* Seek to 'C' at offset 4 */
  istream_seek(s, 4);
  pos = istream_tell(s);
  EXPECT_EQ(pos.byte_offset, 4u);
  EXPECT_EQ(pos.line, 3u);
  EXPECT_EQ(pos.col, 1u);
  EXPECT_EQ(istream_read_cp(s), 'C');

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSourceTest, PositionTracking) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("A\r\nB", 1, 4, wfp);
  fclose(wfp);

  istream_t *s = istream_open(alloc, stream_source_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);

  istream_read_cp(s); /* 'A' */
  EXPECT_EQ(istream_tell(s).line, 1u);
  EXPECT_EQ(istream_tell(s).col, 2u);

  istream_read_cp(s); /* CR+LF */
  EXPECT_EQ(istream_tell(s).line, 2u);
  EXPECT_EQ(istream_tell(s).col, 1u);

  istream_read_cp(s); /* 'B' */
  EXPECT_EQ(istream_tell(s).line, 2u);
  EXPECT_EQ(istream_tell(s).col, 2u);

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSourceTest, Utf8InFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("\xE4\xBD\xA0\xE5\xA5\xBD", 1, 6, wfp);
  fclose(wfp);

  istream_t *s = istream_open(alloc, stream_source_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(istream_read_cp(s), 0x4F60);
  EXPECT_EQ(istream_read_cp(s), 0x597D);
  EXPECT_EQ(istream_read_cp(s), -1);

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSourceTest, ScanfFromFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("42 hello", 1, 8, wfp);
  fclose(wfp);

  istream_t *s = istream_open(alloc, stream_source_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);

  int val = 0;
  int result = istream_scanf(s, "%d", &val);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(val, 42);

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSourceTest, NullPath) {
  allocator_t *alloc = create_allocator(malloc, free);
  stream_source_t src = stream_source_file(alloc, nullptr);
  EXPECT_EQ(src.ctx, nullptr);

  stream_source_t src2 = stream_source_file(nullptr, "dummy");
  EXPECT_EQ(src2.ctx, nullptr);

  delete_allocator(&alloc);
}

TEST(FileSourceTest, NullFp) {
  allocator_t *alloc = create_allocator(malloc, free);
  stream_source_t src = stream_source_file_fp(alloc, nullptr, false);
  EXPECT_EQ(src.ctx, nullptr);

  stream_source_t src2 = stream_source_file_fp(nullptr, stdin, false);
  EXPECT_EQ(src2.ctx, nullptr);

  delete_allocator(&alloc);
}

TEST(FileSourceTest, NonexistentFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  stream_source_t src = stream_source_file(alloc, "/nonexistent/path/to/file.txt");
  EXPECT_EQ(src.ctx, nullptr);
  delete_allocator(&alloc);
}

TEST(FileSourceTest, DataReturnsNull) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;
  FILE *wfp = fopen(path.c_str(), "wb");
  fwrite("X", 1, 1, wfp);
  fclose(wfp);

  stream_source_t src = stream_source_file(alloc, path.c_str());
  ASSERT_NE(src.ctx, nullptr);
  EXPECT_EQ(src.data(src.ctx), nullptr);

  istream_t *s = istream_open(alloc, src);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(istream_data(s), nullptr);

  istream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

/* ---- file sink ---- */

TEST(FileSinkTest, WriteFileByPath) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  stream_sink_t sink = stream_sink_file(alloc, path.c_str());
  ASSERT_NE(sink.ctx, nullptr);

  ostream_t *s = ostream_open(alloc, sink);
  ASSERT_NE(s, nullptr);

  ostream_write_cp(s, 'H');
  ostream_write_cp(s, 'i');
  ostream_close(&s);

  /* Read back and verify */
  FILE *fp = fopen(path.c_str(), "rb");
  ASSERT_NE(fp, nullptr);
  char buf[8] = {0};
  size_t n = fread(buf, 1, sizeof(buf), fp);
  fclose(fp);
  EXPECT_EQ(n, 2u);
  EXPECT_STREQ(buf, "Hi");

  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSinkTest, WriteFileByFp) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  FILE *fp = fopen(path.c_str(), "wb");
  ASSERT_NE(fp, nullptr);

  stream_sink_t sink = stream_sink_file_fp(alloc, fp, true);
  ASSERT_NE(sink.ctx, nullptr);

  ostream_t *s = ostream_open(alloc, sink);
  ASSERT_NE(s, nullptr);

  ostream_write(s, "OK", 2);
  ostream_close(&s);

  /* Read back */
  fp = fopen(path.c_str(), "rb");
  ASSERT_NE(fp, nullptr);
  char buf[8] = {0};
  size_t n = fread(buf, 1, sizeof(buf), fp);
  fclose(fp);
  EXPECT_EQ(n, 2u);
  EXPECT_STREQ(buf, "OK");

  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSinkTest, WriteUtf8ToFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  ostream_t *s = ostream_open(alloc, stream_sink_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);

  ostream_write_cp(s, 0x4F60); /* 你 */
  ostream_write_cp(s, 0x597D); /* 好 */
  ostream_close(&s);

  /* Read back as raw bytes */
  FILE *fp = fopen(path.c_str(), "rb");
  ASSERT_NE(fp, nullptr);
  char buf[16] = {0};
  size_t n = fread(buf, 1, sizeof(buf), fp);
  fclose(fp);
  EXPECT_EQ(n, 6u);
  EXPECT_EQ(memcmp(buf, "\xE4\xBD\xA0\xE5\xA5\xBD", 6), 0);

  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSinkTest, PrintfToFile) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  ostream_t *s = ostream_open(alloc, stream_sink_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);

  ostream_printf(s, "%d + %d = %d", 1, 2, 3);
  ostream_close(&s);

  FILE *fp = fopen(path.c_str(), "rb");
  ASSERT_NE(fp, nullptr);
  char buf[32] = {0};
  size_t n = fread(buf, 1, sizeof(buf), fp);
  fclose(fp);
  EXPECT_EQ(n, 9u);
  EXPECT_STREQ(buf, "1 + 2 = 3");

  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSinkTest, PositionTracking) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  ostream_t *s = ostream_open(alloc, stream_sink_file(alloc, path.c_str()));
  ASSERT_NE(s, nullptr);

  ostream_write_cp(s, 'A');
  stream_pos_t pos = ostream_tell(s);
  EXPECT_EQ(pos.byte_offset, 1u);
  EXPECT_EQ(pos.line, 1u);
  EXPECT_EQ(pos.col, 2u);

  ostream_write_cp(s, '\n');
  pos = ostream_tell(s);
  EXPECT_EQ(pos.line, 2u);
  EXPECT_EQ(pos.col, 1u);

  ostream_write_cp(s, 'B');
  pos = ostream_tell(s);
  EXPECT_EQ(pos.line, 2u);
  EXPECT_EQ(pos.col, 2u);

  ostream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSinkTest, DataReturnsNull) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  stream_sink_t sink = stream_sink_file(alloc, path.c_str());
  ASSERT_NE(sink.ctx, nullptr);
  EXPECT_EQ(sink.data(sink.ctx), nullptr);

  ostream_t *s = ostream_open(alloc, sink);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(ostream_data(s), nullptr);

  ostream_close(&s);
  delete_allocator(&alloc);
  remove(path.c_str());
}

TEST(FileSinkTest, NullPath) {
  allocator_t *alloc = create_allocator(malloc, free);
  stream_sink_t sink = stream_sink_file(alloc, nullptr);
  EXPECT_EQ(sink.ctx, nullptr);

  stream_sink_t sink2 = stream_sink_file(nullptr, "dummy");
  EXPECT_EQ(sink2.ctx, nullptr);

  delete_allocator(&alloc);
}

TEST(FileSinkTest, NullFp) {
  allocator_t *alloc = create_allocator(malloc, free);
  stream_sink_t sink = stream_sink_file_fp(alloc, nullptr, false);
  EXPECT_EQ(sink.ctx, nullptr);

  stream_sink_t sink2 = stream_sink_file_fp(nullptr, stdout, false);
  EXPECT_EQ(sink2.ctx, nullptr);

  delete_allocator(&alloc);
}

/* ---- round-trip: write file then read it back ---- */

TEST(FileRoundTripTest, WriteAndRead) {
  allocator_t *alloc = create_allocator(malloc, free);
  char tmpbuf[256];
  tmpnam(tmpbuf);
  std::string path = tmpbuf;

  /* Write */
  ostream_t *out = ostream_open(alloc, stream_sink_file(alloc, path.c_str()));
  ASSERT_NE(out, nullptr);
  ostream_write(out, "Hello\nWorld", 11);
  ostream_close(&out);

  /* Read */
  istream_t *in = istream_open(alloc, stream_source_file(alloc, path.c_str()));
  ASSERT_NE(in, nullptr);
  EXPECT_EQ(istream_size(in), 11u);

  EXPECT_EQ(istream_read_cp(in), 'H');
  EXPECT_EQ(istream_read_cp(in), 'e');
  EXPECT_EQ(istream_read_cp(in), 'l');
  EXPECT_EQ(istream_read_cp(in), 'l');
  EXPECT_EQ(istream_read_cp(in), 'o');
  EXPECT_EQ(istream_read_cp(in), '\n');
  EXPECT_EQ(istream_tell(in).line, 2u);

  EXPECT_EQ(istream_read_cp(in), 'W');
  EXPECT_EQ(istream_read_cp(in), 'o');
  EXPECT_EQ(istream_read_cp(in), 'r');
  EXPECT_EQ(istream_read_cp(in), 'l');
  EXPECT_EQ(istream_read_cp(in), 'd');
  EXPECT_EQ(istream_read_cp(in), -1);

  istream_close(&in);
  delete_allocator(&alloc);
  remove(path.c_str());
}
