#include "parser/lexer.h"
#include "core/allocator.h"
#include "core/panic.h"
#include "parser/location.h"
#include <string.h>

/* ---- Internal: token_t definition ---- */

struct _token_t {
  token_kind_t kind;
  location_t location;
};

/* ---- Internal: lexer_t definition ---- */

struct _lexer_t {
  allocator_t *allocator;
  istream_t *stream;         /* owned; closed by lexer_close */
  const char *filename;      /* borrowed, must outlive the lexer */
  const char *source_data;   /* borrowed from istream (data accessor) */
  size_t source_len;
  bool eof;                  /* EOF token has been produced */
  token_t *pending;          /* peeked-but-not-consumed token (lexer-owned) */
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
    "as",   "bool", "break", "const",   "continue", "else", "f32",
    "f64",  "false", "for",  "func",    "i16",      "i32",  "i64",
    "i8",   "if",   "return", "str",    "true",     "u16",  "u32",
    "u64",  "u8",   "undefined", "var", "void",     "while",
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

/* ---- Internal: keyword lookup (linear scan; 27 entries) ---- */

static bool lookup_keyword(const char *text, size_t len) {
  for (size_t i = 0; i < sizeof(g_keywords) / sizeof(g_keywords[0]); i++) {
    const char *kw = g_keywords[i];
    if (strlen(kw) == len && memcmp(text, kw, len) == 0)
      return true;
  }
  return false;
}

/* ---- Internal: build a location from begin/end stream positions ---- */

static location_t make_location(const lexer_t *lexer, stream_pos_t begin,
                                stream_pos_t end) {
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

/* ---- Lexer lifecycle ---- */

lexer_t *lexer_create(allocator_t *allocator, istream_t *stream,
                      const char *filename) {
  if (!allocator || !stream)
    return NULL;
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
  return lexer;
}

void lexer_close(lexer_t **lexer) {
  if (!lexer || !*lexer)
    return;
  allocator_free((*lexer)->allocator, (void **)lexer);
}

/* ---- Token production ---- */

token_t *lexer_next(lexer_t *lexer) {
  if (!lexer)
    return NULL;
  if (lexer->pending) {
    token_t *t = lexer->pending;
    lexer->pending = NULL; /* ownership transfers to the caller */
    return t;
  }
  return lexer_read_token(lexer);
}

const token_t *lexer_peek(lexer_t *lexer) {
  if (!lexer)
    return NULL;
  if (!lexer->pending)
    lexer->pending = lexer_read_token(lexer);
  return lexer->pending;
}

/* ---- Internal: read the next raw token from the stream ---- */

static token_t *lexer_read_token(lexer_t *lexer) {
  stream_pos_t begin = istream_tell(lexer->stream);
  if (lexer->eof || istream_at_end(lexer->stream)) {
    lexer->eof = true;
    return create_token(lexer->allocator, TOKEN_TYPE_EOF,
                        make_location(lexer, begin, begin));
  }

  UChar32 cp = istream_peek_cp(lexer->stream);

  if (is_whitespace(cp)) {
    /* Consume and merge the whole run of whitespace into one token. */
    while (is_whitespace(cp)) {
      istream_read_cp(lexer->stream);
      cp = istream_peek_cp(lexer->stream);
      if (cp == -1)
        break;
    }
    stream_pos_t end = istream_tell(lexer->stream);
    return create_token(lexer->allocator, TOKEN_TYPE_WHITESPACE,
                        make_location(lexer, begin, end));
  }

  if (is_ident_start(cp)) {
    istream_read_cp(lexer->stream);
    for (;;) {
      cp = istream_peek_cp(lexer->stream);
      if (cp == -1 || !is_ident_char(cp))
        break;
      istream_read_cp(lexer->stream);
    }
    stream_pos_t end = istream_tell(lexer->stream);
    size_t len = end.byte_offset - begin.byte_offset;
    const char *text = lexer->source_data + begin.byte_offset;
    token_kind_t kind =
        lookup_keyword(text, len) ? TOKEN_TYPE_KEYWORD : TOKEN_TYPE_IDENTIFIER;
    return create_token(lexer->allocator, kind,
                        make_location(lexer, begin, end));
  }

  /* ---- Not yet implemented categories (numeric / string / character /
   * symbol / comment). For now, consume one codepoint and emit an ERROR
   * token; these branches are replaced in later steps. ---- */

  if (cp != -1)
    istream_read_cp(lexer->stream);
  stream_pos_t end = istream_tell(lexer->stream);
  return create_token(lexer->allocator, TOKEN_TYPE_ERROR,
                      make_location(lexer, begin, end));
}

/* ---- Token accessors ---- */

token_kind_t token_get_kind(const token_t *self) {
  if (!self)
    return TOKEN_TYPE_ERROR;
  return self->kind;
}

const location_t *token_get_location(const token_t *self) {
  if (!self)
    return NULL;
  return &self->location;
}

const char *token_get_text(const token_t *self, const lexer_t *lexer,
                           size_t *out_len) {
  if (!self || !lexer) {
    if (out_len)
      *out_len = 0;
    return NULL;
  }
  size_t start = self->location.begin.offset;
  size_t end = self->location.end.offset;
  if (start > end)
    start = end;
  if (end > lexer->source_len)
    end = lexer->source_len;
  if (out_len)
    *out_len = end - start;
  return lexer->source_data + start;
}

bool token_is(const token_t *self, const lexer_t *lexer, const char *str) {
  if (!self || !lexer || !str)
    return false;
  size_t len = strlen(str);
  size_t tlen = 0;
  const char *text = token_get_text(self, lexer, &tlen);
  if (!text)
    return false;
  return tlen == len && memcmp(text, str, len) == 0;
}

/* ---- Token helpers ---- */

token_t *create_token(allocator_t *allocator, token_kind_t kind,
                      location_t location) {
  if (!allocator)
    return NULL;
  token_t *token = (token_t *)allocator_new(allocator, &g_token_class, 1);
  token->kind = kind;
  token->location = location;
  return token;
}

void token_free(allocator_t *allocator, token_t **token) {
  if (!allocator || !token || !*token)
    return;
  allocator_free(allocator, (void **)token);
}

/* ---- Callbacks for lexer_class ---- */

static void lexer_dispose(void *self, allocator_t *allocator) {
  (void)allocator;
  lexer_t *lexer = (lexer_t *)self;
  if (!lexer)
    return;
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
}

static void lexer_move_cb(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  lexer_t *dst = (lexer_t *)self;
  lexer_t *src = (lexer_t *)another;
  if (!dst || !src)
    return;

  dst->allocator = src->allocator;
  dst->stream = src->stream;
  dst->filename = src->filename;
  dst->source_data = src->source_data;
  dst->source_len = src->source_len;
  dst->eof = src->eof;
  dst->pending = src->pending;

  src->allocator = NULL;
  src->stream = NULL;
  src->filename = NULL;
  src->source_data = NULL;
  src->source_len = 0;
  src->eof = false;
  src->pending = NULL;
}
