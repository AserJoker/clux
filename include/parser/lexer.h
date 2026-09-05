#ifndef _H_CLUX_PARSER_LEXER_
#define _H_CLUX_PARSER_LEXER_
#include "core/allocator.h"
#include "core/stream.h"
#include "location.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- Token kinds ---- */

typedef enum {
  TOKEN_TYPE_ERROR, /* lexer error: unrecognized input */
  TOKEN_TYPE_IDENTIFIER,
  TOKEN_TYPE_CHARACTER, /* character literal, e.g. 'a' (value type: u8) */
  TOKEN_TYPE_STRING,    /* string literal, e.g. "abc" */
  TOKEN_TYPE_NUMERIC,   /* integer/float literal; value parsed later */
  TOKEN_TYPE_KEYWORD,
  TOKEN_TYPE_SYMBOL,
  TOKEN_TYPE_COMMENT,           /* line comment: // ... */
  TOKEN_TYPE_MULTILINE_COMMENT, /* block comment: slash-star ... star-slash */
  TOKEN_TYPE_WHITESPACE,        /* one merged run of whitespace */
  TOKEN_TYPE_EOF,
} token_kind_t;

/* ---- Opaque types ---- */

typedef struct _token_t token_t;
typedef struct _lexer_t lexer_t;

/* ---- Lexer lifecycle ---- */

/**
 * Create a lexer over `stream`.
 *
 * The lexer takes ownership of `stream` (closes it on lexer_close).
 * The source must support direct data access (istream_data != NULL),
 * i.e. a memory-backed source; file loading is the caller's job.
 * `filename` is stored by reference (not copied) into every token's
 * location and must outlive the lexer.
 *
 * Returns NULL for invalid arguments or non-direct-access sources.
 * Panics on out-of-memory.
 */
lexer_t *
lexer_create(allocator_t *allocator, istream_t *stream, const char *filename);

/**
 * Close the lexer (closing the underlying istream) and nullify the
 * caller's pointer. No-op if `lexer` or `*lexer` is NULL.
 */
void lexer_close(lexer_t **lexer);

/* ---- Token production ---- */

/**
 * Return the next token. All tokens are produced, including
 * WHITESPACE (consecutive runs merged into one token) and comments.
 * After the input is exhausted, returns TOKEN_TYPE_EOF; repeated calls
 * keep returning EOF (idempotent).
 *
 * The returned token is owned by the caller and must be freed with
 * token_free. Returns NULL only for a NULL lexer.
 */
token_t *lexer_next(lexer_t *lexer);

/**
 * Peek at the next token without consuming it (the lexer position does
 * not advance). Repeated calls return the same token (stable pointer).
 *
 * The returned token is BORROWED: it is owned by the lexer and must
 * NOT be freed or modified. The next lexer_next call transfers
 * ownership of this token to the caller. Returns NULL for a NULL lexer.
 */
const token_t *lexer_peek(lexer_t *lexer);

/* ---- Token accessors ---- */

/** Return the token kind. TOKEN_TYPE_ERROR for NULL. */
token_kind_t token_get_kind(const token_t *self);

/** Return the token location, or NULL. */
const location_t *token_get_location(const token_t *self);

/**
 * Return a slice of the source text covered by the token (zero-copy,
 * O(1)). The slice is NOT NUL-terminated; its length is written to
 * `*out_len`. The pointer is valid as long as the lexer is alive.
 * Returns NULL (and *out_len = 0) on invalid arguments.
 */
const char *
token_get_text(const token_t *self, const lexer_t *lexer, size_t *out_len);

/** Return true if the token text equals `str` (byte-exact). */
bool token_is(const token_t *self, const lexer_t *lexer, const char *str);

/* ---- Token helpers ---- */

/**
 * Create a standalone token (mainly used internally by the lexer and
 * by tests). Panics on out-of-memory. Returns NULL for invalid args.
 */
token_t *
create_token(allocator_t *allocator, token_kind_t kind, location_t location);

/** Free a token and nullify the caller's pointer. No-op if NULL. */
void token_free(allocator_t *allocator, token_t **token);

#ifdef __cplusplus
}
#endif
#endif
