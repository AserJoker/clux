#include "parser/lexer.h"
#include "core/allocator.h"
#include "core/panic.h"
#include "parser/location.h"
#include <stdarg.h>
#include <string.h>

/* ---- Internal: token_t definition ---- */

struct _token_t {
  token_kind_t kind;
  location_t location;
};

/* ---- Internal: lexer_t definition ---- */

struct _lexer_t {
  allocator_t *allocator;
  istream_t *stream;       /* owned; closed by lexer_close */
  const char *filename;    /* borrowed, must outlive the lexer */
  const char *source_data; /* borrowed from istream (data accessor) */
  size_t source_len;
  bool eof;         /* EOF token has been produced */
  token_t *pending; /* peeked-but-not-consumed token (lexer-owned) */

  /* Fatal (fail-fast) error state: once set, the lexer stops producing
   * tokens and never recovers (rewind does not clear it). */
  bool has_error;
  char error_msg[160];
  location_t error_loc;
};

/* ---- Internal: class for token_t ---- */

static class_t g_token_class = {
    .name = "clux.parser.token",
    .size = sizeof(struct _token_t),
    .clone_fn = default_clone,
    .move_fn = default_move,
    .dispose_fn = NULL,
};

/* ---- Internal: class for lexer_t ---- */

static token_t *lexer_read_token(lexer_t *lexer);
static void lexer_dispose(void *self, allocator_t *allocator);
static void lexer_move_cb(void *self, allocator_t *allocator, void *another);

static class_t lexer_class = {
    .name = "clux.parser.lexer",
    .size = sizeof(lexer_t),
    .move_fn = lexer_move_cb,
    .clone_fn = NULL, /* a lexer wraps a consuming stream; not cloneable */
    .dispose_fn = lexer_dispose,
};

/* ---- Internal: keyword table (M1 language keywords) ---- */

static const char *const g_keywords[] = {
    "as",  "bool",  "break",     "const", "continue", "else",  "f32",
    "f64", "false", "for",       "func",  "i16",      "i32",   "i64",
    "i8",  "if",    "return",    "str",   "true",     "u16",   "u32",
    "u64", "u8",    "undefined", "var",   "void",     "while",
};

/* ---- Internal: character classes ---- */

static bool is_ident_start(UChar32 cp) {
  return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

static bool is_ident_char(UChar32 cp) {
  return is_ident_start(cp) || (cp >= '0' && cp <= '9');
}

static bool is_whitespace(UChar32 cp) {
  return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n';
}

static bool is_digit_in_base(UChar32 cp, int base) {
  if (cp >= '0' && cp <= '9') return (cp - '0') < base;
  if (base == 16 && cp >= 'a' && cp <= 'f') return true;
  if (base == 16 && cp >= 'A' && cp <= 'F') return true;
  return false;
}

/* ---- Internal: keyword lookup (linear scan; 27 entries) ---- */

static bool lookup_keyword(const char *text, size_t len) {
  for (size_t i = 0; i < sizeof(g_keywords) / sizeof(g_keywords[0]); i++) {
    const char *kw = g_keywords[i];
    if (strlen(kw) == len && memcmp(text, kw, len) == 0) return true;
  }
  return false;
}

/* ---- Internal: build a location from begin/end stream positions ---- */

static location_t
make_location(const lexer_t *lexer, stream_pos_t begin, stream_pos_t end) {
  location_t loc;
  loc.begin.offset = begin.byte_offset;
  loc.begin.line = begin.line;
  loc.begin.column = begin.cluster_col; /* grapheme-cluster column */
  loc.end.offset = end.byte_offset;
  loc.end.line = end.line;
  loc.end.column = end.cluster_col;
  loc.filename = lexer->filename;
  return loc;
}

/* ---- Internal: fatal error (fail-fast) ---- */

/**
 * Record a lexical error and return a TOKEN_TYPE_ERROR token covering
 * the error position. The lexer enters its permanent error state: every
 * subsequent lexer_next / lexer_peek returns EOF, and lexer_rewind does
 * not clear the error. The error message is available via lexer_error.
 */
static token_t *
lexer_fail(lexer_t *lexer, stream_pos_t at, const char *fmt, ...) {
  if (!lexer->has_error) {
    lexer->has_error = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lexer->error_msg, sizeof(lexer->error_msg), fmt, ap);
    va_end(ap);
    lexer->error_loc = make_location(lexer, at, at);
  }
  lexer->eof = true; /* stop: every later call returns EOF */
  return create_token(lexer->allocator, TOKEN_TYPE_ERROR, lexer->error_loc);
}

/* ---- Lexer lifecycle ---- */

lexer_t *
lexer_create(allocator_t *allocator, istream_t *stream, const char *filename) {
  if (!allocator || !stream) return NULL;
  const char *data = istream_data(stream);
  if (!data)
    return NULL; /* requires a source with direct data access (mem-backed) */

  lexer_t *lexer = (lexer_t *)allocator_new(allocator, &lexer_class, 1);
  lexer->allocator = allocator;
  lexer->stream = stream;
  lexer->filename = filename;
  lexer->source_data = data;
  lexer->source_len = istream_size(stream);
  lexer->eof = false;
  lexer->pending = NULL;
  lexer->has_error = false;
  lexer->error_msg[0] = '\0';
  memset(&lexer->error_loc, 0, sizeof(lexer->error_loc));
  return lexer;
}

void lexer_close(lexer_t **lexer) {
  if (!lexer || !*lexer) return;
  allocator_free((*lexer)->allocator, (void **)lexer);
}

/* ---- Token production ---- */

token_t *lexer_next(lexer_t *lexer) {
  if (!lexer) return NULL;
  if (lexer->pending) {
    token_t *t = lexer->pending;
    lexer->pending = NULL; /* ownership transfers to the caller */
    return t;
  }
  return lexer_read_token(lexer);
}

const token_t *lexer_peek(lexer_t *lexer) {
  if (!lexer) return NULL;
  if (!lexer->pending) lexer->pending = lexer_read_token(lexer);
  return lexer->pending;
}

/* ---- Backtracking ---- */

lexer_checkpoint_t lexer_checkpoint(const lexer_t *lexer) {
  lexer_checkpoint_t cp = {0};
  if (!lexer) return cp;
  if (lexer->pending) {
    /* The next token to be produced is the peeked one: the checkpoint
     * must record its start, not the stream position (which already
     * points past it). */
    cp.pos.byte_offset = lexer->pending->location.begin.offset;
    cp.pos.line = lexer->pending->location.begin.line;
    cp.pos.col = lexer->pending->location.begin.column;
    cp.pos.cluster_col =
        cp.pos.col; /* approximate; not used by rewind (recomputed) */
  } else {
    cp.pos = istream_tell(lexer->stream);
  }
  cp.eof = lexer->eof;
  return cp;
}

void lexer_rewind(lexer_t *lexer, lexer_checkpoint_t checkpoint) {
  if (!lexer) return;
  /* Drop any peeked-but-unconsumed token: after a rewind the next
   * peek/next re-lexes from the checkpoint, so a stale pending token
   * would be wrong. */
  if (lexer->pending) {
    token_t *t = lexer->pending;
    token_free(lexer->allocator, &t);
    lexer->pending = NULL;
  }
  istream_seek(lexer->stream, checkpoint.pos.byte_offset);
  lexer->eof = checkpoint.eof;
  /* NOTE: has_error / error_msg are intentionally NOT cleared. Lexical
   * errors are permanent (fail-fast); a rewind must not revive a lexer
   * that already hit an unrecoverable error. */
}

/* ---- Fatal error accessor ---- */

const char *lexer_error(const lexer_t *lexer, location_t *out_loc) {
  if (!lexer || !lexer->has_error) return NULL;
  if (out_loc) *out_loc = lexer->error_loc;
  return lexer->error_msg;
}

/* ---- Internal: numeric literals ---- */

static bool suffix_valid(const char *s, size_t len, bool is_float) {
  static const char *const kIntSuffix[] = {
      "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"};
  static const char *const kFltSuffix[] = {"f32", "f64"};
  size_t i;
  if (is_float) {
    for (i = 0; i < sizeof(kFltSuffix) / sizeof(kFltSuffix[0]); i++)
      if (strlen(kFltSuffix[i]) == len && memcmp(s, kFltSuffix[i], len) == 0)
        return true;
  } else {
    for (i = 0; i < sizeof(kIntSuffix) / sizeof(kIntSuffix[0]); i++)
      if (strlen(kIntSuffix[i]) == len && memcmp(s, kIntSuffix[i], len) == 0)
        return true;
  }
  return false;
}

static token_t *lexer_read_number(lexer_t *lexer, stream_pos_t begin) {
  int base = 10;
  bool is_float = false;
  UChar32 cp = istream_read_cp(lexer->stream); /* leading digit */

  /* 0x / 0o / 0b base prefix */
  if (cp == '0') {
    UChar32 nx = istream_peek_cp(lexer->stream);
    if (nx == 'x' || nx == 'X') {
      base = 16;
      istream_read_cp(lexer->stream);
    } else if (nx == 'o' || nx == 'O') {
      base = 8;
      istream_read_cp(lexer->stream);
    } else if (nx == 'b' || nx == 'B') {
      base = 2;
      istream_read_cp(lexer->stream);
    }
  }

  /* integer digits */
  for (;;) {
    cp = istream_peek_cp(lexer->stream);
    if (cp == -1 || !is_digit_in_base(cp, base)) break;
    istream_read_cp(lexer->stream);
  }

  if (base != 10) {
    /* non-decimal literals need at least one digit after the prefix */
    if (istream_tell(lexer->stream).byte_offset == begin.byte_offset + 2)
      return lexer_fail(
          lexer, begin, "invalid numeric literal: no digits after prefix");
    goto suffix;
  }

  /* fractional part */
  cp = istream_peek_cp(lexer->stream);
  if (cp == '.') {
    istream_read_cp(lexer->stream);
    UChar32 d = istream_peek_cp(lexer->stream);
    if (d == -1 || !is_digit_in_base(d, 10))
      return lexer_fail(lexer, begin, "expected digit after decimal point");
    is_float = true;
    for (;;) {
      d = istream_peek_cp(lexer->stream);
      if (d == -1 || !is_digit_in_base(d, 10)) break;
      istream_read_cp(lexer->stream);
    }
  }

  /* exponent */
  cp = istream_peek_cp(lexer->stream);
  if (cp == 'e' || cp == 'E') {
    istream_read_cp(lexer->stream);
    is_float = true;
    UChar32 s = istream_peek_cp(lexer->stream);
    if (s == '+' || s == '-') istream_read_cp(lexer->stream);
    UChar32 d = istream_peek_cp(lexer->stream);
    if (d == -1 || !is_digit_in_base(d, 10))
      return lexer_fail(lexer, begin, "expected digit in exponent");
    for (;;) {
      d = istream_peek_cp(lexer->stream);
      if (d == -1 || !is_digit_in_base(d, 10)) break;
      istream_read_cp(lexer->stream);
    }
  }

suffix:
  /* type suffix: [a-zA-Z0-9_]* immediately after the digits */
  {
    size_t suf_begin = istream_tell(lexer->stream).byte_offset;
    size_t suf_len = 0;
    for (;;) {
      cp = istream_peek_cp(lexer->stream);
      if (cp == -1 || !is_ident_char(cp)) break;
      istream_read_cp(lexer->stream);
      suf_len++;
    }
    if (suf_len > 0) {
      const char *suf = lexer->source_data + suf_begin;
      if (!suffix_valid(suf, suf_len, is_float))
        return lexer_fail(lexer,
                          begin,
                          "invalid numeric literal suffix '%.*s'",
                          (int)suf_len,
                          suf);
    }
  }

  stream_pos_t end = istream_tell(lexer->stream);
  return create_token(
      lexer->allocator, TOKEN_TYPE_NUMERIC, make_location(lexer, begin, end));
}

/* ---- Internal: string literals ---- */

static token_t *lexer_read_string(lexer_t *lexer, stream_pos_t begin) {
  istream_read_cp(lexer->stream); /* consume '"' */
  for (;;) {
    UChar32 cp = istream_read_cp(lexer->stream);
    if (cp == -1)
      return lexer_fail(lexer, begin, "unterminated string literal");
    if (cp == '"') break;
    if (cp == '\\') {
      if (istream_read_cp(lexer->stream) == -1)
        return lexer_fail(
            lexer, begin, "unterminated string literal (dangling backslash)");
    } else if (cp == '\n' || cp == '\r') {
      return lexer_fail(
          lexer, begin, "unterminated string literal (newline in string)");
    }
  }
  stream_pos_t end = istream_tell(lexer->stream);
  return create_token(
      lexer->allocator, TOKEN_TYPE_STRING, make_location(lexer, begin, end));
}

/* ---- Internal: character literals ---- */

static token_t *lexer_read_char(lexer_t *lexer, stream_pos_t begin) {
  istream_read_cp(lexer->stream); /* consume '\'' */
  for (;;) {
    UChar32 cp = istream_read_cp(lexer->stream);
    if (cp == -1)
      return lexer_fail(lexer, begin, "unterminated character literal");
    if (cp == '\'') break;
    if (cp == '\\') {
      if (istream_read_cp(lexer->stream) == -1)
        return lexer_fail(lexer,
                          begin,
                          "unterminated character literal (dangling "
                          "backslash)");
    } else if (cp == '\n' || cp == '\r') {
      return lexer_fail(
          lexer, begin, "unterminated character literal (newline in literal)");
    }
  }
  stream_pos_t end = istream_tell(lexer->stream);
  return create_token(
      lexer->allocator, TOKEN_TYPE_CHARACTER, make_location(lexer, begin, end));
}

/* ---- Internal: '/' dispatch: comments and symbol ---- */

static token_t *lexer_read_slash(lexer_t *lexer, stream_pos_t begin) {
  /* leading '/' is peeked by the caller; consume it */
  istream_read_cp(lexer->stream);
  UChar32 cp = istream_peek_cp(lexer->stream);

  if (cp == '/') { /* line comment: // ... up to (not incl.) newline or EOF */
    istream_read_cp(lexer->stream);
    for (;;) {
      cp = istream_peek_cp(lexer->stream);
      if (cp == -1 || cp == '\n') break;
      istream_read_cp(lexer->stream);
    }
    stream_pos_t end = istream_tell(lexer->stream);
    return create_token(
        lexer->allocator, TOKEN_TYPE_COMMENT, make_location(lexer, begin, end));
  }

  if (cp == '*') { /* block comment: slash-star ... star-slash, nestable */
    istream_read_cp(lexer->stream);
    int depth = 1;
    for (;;) {
      cp = istream_read_cp(lexer->stream);
      if (cp == -1)
        return lexer_fail(lexer, begin, "unterminated block comment");
      if (cp == '/') {
        if (istream_peek_cp(lexer->stream) == '*') {
          istream_read_cp(lexer->stream);
          depth++;
        }
      } else if (cp == '*') {
        if (istream_peek_cp(lexer->stream) == '/') {
          istream_read_cp(lexer->stream);
          if (--depth == 0) break;
        }
      }
    }
    stream_pos_t end = istream_tell(lexer->stream);
    return create_token(lexer->allocator,
                        TOKEN_TYPE_MULTILINE_COMMENT,
                        make_location(lexer, begin, end));
  }

  if (cp == '=') { /* '/=' */
    istream_read_cp(lexer->stream);
    stream_pos_t end = istream_tell(lexer->stream);
    return create_token(
        lexer->allocator, TOKEN_TYPE_SYMBOL, make_location(lexer, begin, end));
  }

  /* single '/' */
  stream_pos_t end = istream_tell(lexer->stream);
  return create_token(
      lexer->allocator, TOKEN_TYPE_SYMBOL, make_location(lexer, begin, end));
}

/* ---- Internal: symbols (maximal munch) ---- */

static bool is_single_symbol(UChar32 c) {
  switch (c) {
  case '+':
  case '-':
  case '*':
  case '%':
  case '<':
  case '>':
  case '=':
  case '!':
  case '&':
  case '|':
  case '^':
  case '~':
  case '(':
  case ')':
  case '{':
  case '}':
  case '[':
  case ']':
  case ';':
  case ',':
  case ':':
    return true;
  default:
    return false;
  }
}

static token_t *lexer_read_symbol(lexer_t *lexer, stream_pos_t begin) {
  static const char *const kPairs[] = {
      "<<",
      ">>",
      "<=",
      ">=",
      "==",
      "!=",
      "&&",
      "||",
      "+=",
      "-=",
      "*=",
      "/=",
      "%=",
  };
  UChar32 c1 = istream_read_cp(lexer->stream);
  UChar32 c2 = istream_peek_cp(lexer->stream);

  if (c2 != -1) {
    for (size_t i = 0; i < sizeof(kPairs) / sizeof(kPairs[0]); i++) {
      if ((UChar32)kPairs[i][0] == c1 && (UChar32)kPairs[i][1] == c2) {
        istream_read_cp(lexer->stream);
        stream_pos_t end = istream_tell(lexer->stream);
        return create_token(lexer->allocator,
                            TOKEN_TYPE_SYMBOL,
                            make_location(lexer, begin, end));
      }
    }
  }

  if (is_single_symbol(c1)) {
    stream_pos_t end = istream_tell(lexer->stream);
    return create_token(
        lexer->allocator, TOKEN_TYPE_SYMBOL, make_location(lexer, begin, end));
  }

  return lexer_fail(lexer, begin, "unrecognized character U+%04X", (int)c1);
}

/* ---- Internal: read the next raw token from the stream ---- */

static token_t *lexer_read_token(lexer_t *lexer) {
  stream_pos_t begin = istream_tell(lexer->stream);
  if (lexer->eof || istream_at_end(lexer->stream)) {
    lexer->eof = true;
    return create_token(
        lexer->allocator, TOKEN_TYPE_EOF, make_location(lexer, begin, begin));
  }

  UChar32 cp = istream_peek_cp(lexer->stream);

  if (is_whitespace(cp)) {
    /* Consume and merge the whole run of whitespace into one token. */
    while (is_whitespace(cp)) {
      istream_read_cp(lexer->stream);
      cp = istream_peek_cp(lexer->stream);
      if (cp == -1) break;
    }
    stream_pos_t end = istream_tell(lexer->stream);
    return create_token(lexer->allocator,
                        TOKEN_TYPE_WHITESPACE,
                        make_location(lexer, begin, end));
  }

  if (is_ident_start(cp)) {
    istream_read_cp(lexer->stream);
    for (;;) {
      cp = istream_peek_cp(lexer->stream);
      if (cp == -1 || !is_ident_char(cp)) break;
      istream_read_cp(lexer->stream);
    }
    stream_pos_t end = istream_tell(lexer->stream);
    size_t len = end.byte_offset - begin.byte_offset;
    const char *text = lexer->source_data + begin.byte_offset;
    token_kind_t kind =
        lookup_keyword(text, len) ? TOKEN_TYPE_KEYWORD : TOKEN_TYPE_IDENTIFIER;
    return create_token(
        lexer->allocator, kind, make_location(lexer, begin, end));
  }

  if (cp >= '0' && cp <= '9') return lexer_read_number(lexer, begin);

  switch (cp) {
  case '"':
    return lexer_read_string(lexer, begin);
  case '\'':
    return lexer_read_char(lexer, begin);
  case '/':
    return lexer_read_slash(lexer, begin);
  default:
    return lexer_read_symbol(lexer, begin);
  }
}

/* ---- Token accessors ---- */

token_kind_t token_get_kind(const token_t *self) {
  if (!self) return TOKEN_TYPE_ERROR;
  return self->kind;
}

const location_t *token_get_location(const token_t *self) {
  if (!self) return NULL;
  return &self->location;
}

const char *
token_get_text(const token_t *self, const lexer_t *lexer, size_t *out_len) {
  if (!self || !lexer) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  size_t start = self->location.begin.offset;
  size_t end = self->location.end.offset;
  if (start > end) start = end;
  if (end > lexer->source_len) end = lexer->source_len;
  if (out_len) *out_len = end - start;
  return lexer->source_data + start;
}

bool token_is(const token_t *self, const lexer_t *lexer, const char *str) {
  if (!self || !lexer || !str) return false;
  size_t len = strlen(str);
  size_t tlen = 0;
  const char *text = token_get_text(self, lexer, &tlen);
  if (!text) return false;
  return tlen == len && memcmp(text, str, len) == 0;
}

/* ---- Token helpers ---- */

token_t *
create_token(allocator_t *allocator, token_kind_t kind, location_t location) {
  if (!allocator) return NULL;
  token_t *token = (token_t *)allocator_new(allocator, &g_token_class, 1);
  token->kind = kind;
  token->location = location;
  return token;
}

void token_free(allocator_t *allocator, token_t **token) {
  if (!allocator || !token || !*token) return;
  allocator_free(allocator, (void **)token);
}

/* ---- Callbacks for lexer_class ---- */

static void lexer_dispose(void *self, allocator_t *allocator) {
  (void)allocator;
  lexer_t *lexer = (lexer_t *)self;
  if (!lexer) return;
  if (lexer->pending) {
    token_t *t = lexer->pending;
    token_free(lexer->allocator, &t);
    lexer->pending = NULL;
  }
  if (lexer->stream) {
    istream_t *s = lexer->stream;
    istream_close(&s);
    lexer->stream = NULL;
  }
  lexer->allocator = NULL;
  lexer->filename = NULL;
  lexer->source_data = NULL;
  lexer->source_len = 0;
  lexer->eof = false;
  lexer->has_error = false;
  lexer->error_msg[0] = '\0';
}

static void lexer_move_cb(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  lexer_t *dst = (lexer_t *)self;
  lexer_t *src = (lexer_t *)another;
  if (!dst || !src) return;

  dst->allocator = src->allocator;
  dst->stream = src->stream;
  dst->filename = src->filename;
  dst->source_data = src->source_data;
  dst->source_len = src->source_len;
  dst->eof = src->eof;
  dst->pending = src->pending;
  dst->has_error = src->has_error;
  memcpy(dst->error_msg, src->error_msg, sizeof(dst->error_msg));
  dst->error_loc = src->error_loc;

  src->allocator = NULL;
  src->stream = NULL;
  src->filename = NULL;
  src->source_data = NULL;
  src->source_len = 0;
  src->eof = false;
  src->pending = NULL;
  src->has_error = false;
  src->error_msg[0] = '\0';
  memset(&src->error_loc, 0, sizeof(src->error_loc));
}
