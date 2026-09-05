#include <gtest/gtest.h>
#include <string>

extern "C" {
#include "core/allocator.h"
#include "core/stream.h"
#include "parser/location.h"
#include "parser/lexer.h"
}

/* ---- Test allocator helpers ---- */

static void *test_alloc(size_t size) { return malloc(size); }
static void test_free(void *ptr) { free(ptr); }

/* ---- Helpers ---- */

static istream_t *make_istream(allocator_t *a, const char *text) {
  stream_source_t src = stream_source_mem(a, text, strlen(text), false);
  return istream_open(a, src);
}

static lexer_t *
make_lexer(allocator_t *a, const char *text, const char *filename) {
  return lexer_create(a, make_istream(a, text), filename);
}

static token_t *take(allocator_t *a,
                     lexer_t *lx,
                     token_kind_t expected,
                     const char *expected_text) {
  token_t *t = lexer_next(lx);
  EXPECT_NE(t, nullptr);
  if (!t) return nullptr;
  EXPECT_EQ(token_get_kind(t), expected);
  size_t len = 0;
  const char *text = token_get_text(t, lx, &len);
  if (expected_text) {
    EXPECT_EQ(len, strlen(expected_text));
    EXPECT_EQ(memcmp(text, expected_text, len), 0);
  }
  return t;
}

/* ==== Lexer create / close ==== */

TEST(Lexer, CreateNullSafe) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(lexer_create(NULL, NULL, "x"), nullptr);
  EXPECT_EQ(lexer_create(a, NULL, "x"), nullptr);
  lexer_close(nullptr);
  lexer_t *null_lexer = nullptr;
  lexer_close(&null_lexer); /* no-op */
  EXPECT_EQ(null_lexer, nullptr);
  delete_allocator(&a);
}

TEST(Lexer, CreateClosesStreamOnClose) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "abc", "t.cx");
  ASSERT_NE(lx, nullptr);
  lexer_close(&lx);
  EXPECT_EQ(lx, nullptr);
  delete_allocator(&a); /* no leak: lexer owns the istream */
}

/* ==== EOF ==== */

TEST(Lexer, EmptyInputEof) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "", "t.cx");
  token_t *t = lexer_next(lx);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  const location_t *loc = token_get_location(t);
  EXPECT_EQ(loc->begin.offset, loc->end.offset); /* begin == end */
  EXPECT_EQ(loc->begin.line, loc->end.line);
  EXPECT_EQ(loc->begin.column, loc->end.column);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, EofIdempotent) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "x", "t.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "x");
  token_free(a, &t);
  for (int i = 0; i < 5; i++) {
    t = lexer_next(lx);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
    token_free(a, &t);
  }
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Keywords ==== */

TEST(Lexer, AllKeywords) {
  const char *kws[] = {
      "as",  "bool",  "break",     "const", "continue", "else",  "f32",
      "f64", "false", "for",       "func",  "i16",      "i32",   "i64",
      "i8",  "if",    "return",    "str",   "true",     "u16",   "u32",
      "u64", "u8",    "undefined", "var",   "void",     "while",
  };
  allocator_t *a = create_allocator(test_alloc, test_free);
  for (size_t i = 0; i < sizeof(kws) / sizeof(kws[0]); i++) {
    lexer_t *lx = make_lexer(a, kws[i], "kw.cx");
    token_t *t = take(a, lx, TOKEN_TYPE_KEYWORD, kws[i]);
    token_free(a, &t);
    lexer_close(&lx);
  }
  delete_allocator(&a);
}

TEST(Lexer, KeywordVsIdentifier) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "if iffy if9", "t.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_KEYWORD, "if");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "iffy");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "if9");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Identifiers ==== */

TEST(Lexer, Identifiers) {
  const char *idents[] = {"foo", "foo123", "_x", "x_y", "Foo", "a1_b2"};
  allocator_t *a = create_allocator(test_alloc, test_free);
  for (size_t i = 0; i < sizeof(idents) / sizeof(idents[0]); i++) {
    lexer_t *lx = make_lexer(a, idents[i], "id.cx");
    token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, idents[i]);
    token_free(a, &t);
    lexer_close(&lx);
  }
  delete_allocator(&a);
}

TEST(Lexer, IdentifierStopsAtNonIdent) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  /* '=' is not implemented yet -> ERROR placeholder; only check that the
   * identifier itself stops at the boundary. */
  lexer_t *lx = make_lexer(a, "abc=", "t.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "abc");
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Whitespace ==== */

TEST(Lexer, WhitespaceMergedIntoOneToken) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  const char *ws = " \t\n\r\n ";
  lexer_t *lx = make_lexer(a, ws, "ws.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_WHITESPACE, ws);
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, WhitespaceLocation) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "ab\ncd", "ws.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "ab");
  token_free(a, &t);

  t = lexer_next(lx);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_WHITESPACE);
  const location_t *loc = token_get_location(t);
  EXPECT_EQ(loc->begin.offset, 2u);
  EXPECT_EQ(loc->begin.line, 1u);
  EXPECT_EQ(loc->begin.column, 3u);
  EXPECT_EQ(loc->end.offset, 3u);
  EXPECT_EQ(loc->end.line, 2u);
  EXPECT_EQ(loc->end.column, 1u);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Locations ==== */

TEST(Lexer, LocationHalfOpenRange) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "ab\ncd", "loc.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "ab");
  const location_t *loc = token_get_location(t);
  EXPECT_EQ(loc->begin.offset, 0u);
  EXPECT_EQ(loc->begin.line, 1u);
  EXPECT_EQ(loc->begin.column, 1u);
  EXPECT_EQ(loc->end.offset, 2u); /* half-open [0, 2) */
  EXPECT_EQ(loc->end.line, 1u);
  EXPECT_EQ(loc->end.column, 3u);
  EXPECT_STREQ(loc->filename, "loc.cx");
  token_free(a, &t);

  t = lexer_next(lx); /* whitespace "\n" */
  token_free(a, &t);

  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "cd");
  loc = token_get_location(t);
  EXPECT_EQ(loc->begin.offset, 3u);
  EXPECT_EQ(loc->begin.line, 2u);
  EXPECT_EQ(loc->begin.column, 1u);
  EXPECT_EQ(loc->end.offset, 5u);
  EXPECT_EQ(loc->end.line, 2u);
  EXPECT_EQ(loc->end.column, 3u);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Mixed stream ==== */

TEST(Lexer, MixedStream) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "func foo", "mix.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_KEYWORD, "func");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== token_is ==== */

TEST(Lexer, TokenIs) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "func", "t.cx");
  token_t *t = lexer_next(lx);
  ASSERT_NE(t, nullptr);
  EXPECT_TRUE(token_is(t, lx, "func"));
  EXPECT_FALSE(token_is(t, lx, "funcx"));
  EXPECT_FALSE(token_is(t, lx, "fun"));
  EXPECT_FALSE(token_is(t, lx, ""));
  EXPECT_FALSE(token_is(t, lx, nullptr));
  EXPECT_FALSE(token_is(nullptr, lx, "func"));
  EXPECT_FALSE(token_is(t, nullptr, "func"));
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Error token (unrecognized characters) ==== */

TEST(Lexer, UnrecognizedCharProducesError) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "@x", "err.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_ERROR, "@");
  const location_t *loc = token_get_location(t);
  EXPECT_EQ(loc->begin.offset, 0u);
  EXPECT_EQ(loc->end.offset, 1u); /* consumes one codepoint */
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "x");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== Null safety ==== */

TEST(Lexer, NullSafety) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  EXPECT_EQ(lexer_next(nullptr), nullptr);
  EXPECT_EQ(token_get_kind(nullptr), TOKEN_TYPE_ERROR);
  EXPECT_EQ(token_get_location(nullptr), nullptr);

  lexer_t *lx = make_lexer(a, "abc", "t.cx");
  token_t *t = lexer_next(lx);
  ASSERT_NE(t, nullptr);

  size_t len = 123;
  EXPECT_EQ(token_get_text(nullptr, lx, &len), nullptr);
  EXPECT_EQ(len, 0u);
  EXPECT_EQ(token_get_text(t, nullptr, &len), nullptr);
  EXPECT_EQ(len, 0u);
  EXPECT_EQ(token_get_text(t, lx, nullptr), token_get_text(t, lx, &len));
  EXPECT_EQ(len, 3u); /* out_len may be NULL: still returns the slice */

  token_free(a, &t);
  token_free(a, nullptr); /* no-op */
  token_t *null_tok = nullptr;
  token_free(a, &null_tok); /* no-op */
  EXPECT_EQ(null_tok, nullptr);
  token_free(nullptr, &null_tok); /* no-op */
  EXPECT_EQ(create_token(nullptr, TOKEN_TYPE_EOF, (location_t){0}), nullptr);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== lexer_peek ==== */

TEST(Lexer, PeekStablePointer) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "func", "peek.cx");
  const token_t *p1 = lexer_peek(lx);
  const token_t *p2 = lexer_peek(lx);
  ASSERT_NE(p1, nullptr);
  EXPECT_EQ(p1, p2); /* repeated peeks return the same token */
  EXPECT_EQ(token_get_kind(p1), TOKEN_TYPE_KEYWORD);
  EXPECT_TRUE(token_is(p1, lx, "func"));
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, PeekDoesNotAdvance) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "foo bar", "peek.cx");
  const token_t *p = lexer_peek(lx);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(token_get_kind(p), TOKEN_TYPE_IDENTIFIER);
  EXPECT_TRUE(token_is(p, lx, "foo"));

  /* next still sees the same first token */
  token_t *t = lexer_next(lx);
  EXPECT_EQ(t, p); /* ownership of the peeked token transfers */
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_IDENTIFIER);
  token_free(a, &t);

  /* the lexer position advanced past "foo" */
  const token_t *p2 = lexer_peek(lx);
  ASSERT_NE(p2, nullptr);
  EXPECT_EQ(token_get_kind(p2), TOKEN_TYPE_WHITESPACE);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, PeekAcrossTokens) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "a b", "peek.cx");
  const token_t *p = lexer_peek(lx);
  EXPECT_TRUE(token_is(p, lx, "a"));
  token_t *t = lexer_next(lx);
  token_free(a, &t);

  p = lexer_peek(lx);
  EXPECT_TRUE(token_is(p, lx, " "));
  t = lexer_next(lx);
  token_free(a, &t);

  p = lexer_peek(lx);
  EXPECT_TRUE(token_is(p, lx, "b"));
  t = lexer_next(lx);
  token_free(a, &t);

  p = lexer_peek(lx);
  EXPECT_EQ(token_get_kind(p), TOKEN_TYPE_EOF);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);

  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, PeekEofStable) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "", "peek.cx");
  const token_t *p1 = lexer_peek(lx);
  ASSERT_NE(p1, nullptr);
  EXPECT_EQ(token_get_kind(p1), TOKEN_TYPE_EOF);
  const token_t *p2 = lexer_peek(lx);
  EXPECT_EQ(p1, p2);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, PeekNullSafe) { EXPECT_EQ(lexer_peek(nullptr), nullptr); }

TEST(Lexer, PeekWorksAfterMove) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "return", "peekmv.cx");
  lexer_t *moved = (lexer_t *)allocator_move(a, (void **)&lx);
  ASSERT_NE(moved, nullptr);
  const token_t *p = lexer_peek(moved);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(token_get_kind(p), TOKEN_TYPE_KEYWORD);
  EXPECT_TRUE(token_is(p, moved, "return"));
  token_t *t = lexer_next(moved);
  EXPECT_EQ(t, p);
  token_free(a, &t);
  lexer_close(&moved);
  delete_allocator(&a);
}

/* ==== Backtracking (checkpoint / rewind) ==== */

TEST(Lexer, CheckpointNullSafe) {
  lexer_checkpoint_t cp = lexer_checkpoint(nullptr);
  EXPECT_EQ(cp.pos.byte_offset, 0u);
  EXPECT_EQ(cp.eof, false);
}

TEST(Lexer, RewindNullSafe) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "foo", "rw.cx");
  lexer_checkpoint_t cp = lexer_checkpoint(lx);
  lexer_rewind(lx, cp);        /* no-op-ish: rewind to the same point */
  lexer_rewind(nullptr, cp);   /* no-op */
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, RewindReplaysTokens) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "foo bar baz", "rw.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);

  lexer_checkpoint_t cp = lexer_checkpoint(lx); /* before "bar" */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "bar");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "baz");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);

  lexer_rewind(lx, cp); /* the same tokens are produced again */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "bar");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "baz");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, RewindAfterPeekReProducesPeekedToken) {
  /* The parser has peeked "func", then checkpoints. A rewind must put
   * the lexer back BEFORE "func" — the pending token must not be lost. */
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "func foo", "rw.cx");
  const token_t *p = lexer_peek(lx);
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(token_is(p, lx, "func"));

  lexer_checkpoint_t cp = lexer_checkpoint(lx); /* pending = "func" */
  token_t *t = lexer_next(lx);
  EXPECT_EQ(t, p); /* ownership of the peeked token transfers */
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);

  lexer_rewind(lx, cp); /* must re-produce "func" first */
  t = take(a, lx, TOKEN_TYPE_KEYWORD, "func");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, RewindDropsPendingToken) {
  /* A pending token at rewind time is lexer-owned and must be dropped,
   * not leak into the re-lexed stream. */
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "foo bar", "rw.cx");
  lexer_checkpoint_t cp = lexer_checkpoint(lx); /* before "foo" */
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  const token_t *p = lexer_peek(lx); /* pending = "bar" */
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(token_is(p, lx, "bar"));

  lexer_rewind(lx, cp); /* drops pending "bar", back to "foo" */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "bar");
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, RewindAfterEof) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "foo", "rw.cx");
  lexer_checkpoint_t cp = lexer_checkpoint(lx);
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF); /* idempotent */
  token_free(a, &t);

  lexer_rewind(lx, cp); /* EOF flag must be cleared along with the seek */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "foo");
  token_free(a, &t);
  t = lexer_next(lx);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, RewindRestoresLocation) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "ab\ncd\nef", "rw.cx");
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "ab");
  token_free(a, &t);
  t = lexer_next(lx); /* "\n" */
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "cd");
  token_free(a, &t);
  t = lexer_next(lx); /* "\n" */
  token_free(a, &t);

  lexer_checkpoint_t cp = lexer_checkpoint(lx); /* before "ef" */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "ef");
  const location_t *loc = token_get_location(t);
  EXPECT_EQ(loc->begin.line, 3u);
  EXPECT_EQ(loc->begin.column, 1u);
  token_free(a, &t);

  lexer_rewind(lx, cp);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "ef");
  loc = token_get_location(t);
  EXPECT_EQ(loc->begin.offset, 6u);
  EXPECT_EQ(loc->begin.line, 3u); /* line/col recomputed by the seek */
  EXPECT_EQ(loc->begin.column, 1u);
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

TEST(Lexer, NestedCheckpoints) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "a b c d", "rw.cx");
  lexer_checkpoint_t cp_a = lexer_checkpoint(lx); /* before "a" */
  token_t *t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "a");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);

  lexer_checkpoint_t cp_b = lexer_checkpoint(lx); /* before "b" */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "b");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "c");
  token_free(a, &t);

  lexer_rewind(lx, cp_b); /* inner rewind: back to before "b" */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "b");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "c");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "d");
  token_free(a, &t);

  lexer_rewind(lx, cp_a); /* outer rewind: back to the very start */
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "a");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_WHITESPACE, " ");
  token_free(a, &t);
  t = take(a, lx, TOKEN_TYPE_IDENTIFIER, "b");
  token_free(a, &t);
  lexer_close(&lx);
  delete_allocator(&a);
}

/* ==== allocator_move on lexer ==== */

TEST(Lexer, MoveTransfersOwnership) {
  allocator_t *a = create_allocator(test_alloc, test_free);
  lexer_t *lx = make_lexer(a, "func", "mv.cx");
  lexer_t *moved = (lexer_t *)allocator_move(a, (void **)&lx);
  ASSERT_NE(moved, nullptr);
  EXPECT_EQ(lx, nullptr); /* source nullified */

  token_t *t = take(a, moved, TOKEN_TYPE_KEYWORD, "func");
  token_free(a, &t);
  t = lexer_next(moved);
  EXPECT_EQ(token_get_kind(t), TOKEN_TYPE_EOF);
  token_free(a, &t);
  lexer_close(&moved);
  delete_allocator(&a);
}
