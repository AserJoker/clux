#include "driver/driver.h"
#include "core/allocator.h"
#include "core/stream.h"
#include "core/vec.h"
#include "parser/lexer.h"
#include "parser/location.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Internal: byte buffer class (for file-backed source buffers) ---- */

static class_t g_bytes_class = {
    .name = "clux.driver.bytes",
    .size = sizeof(char),
    .clone_fn = default_clone,
    .move_fn = default_move,
    .dispose_fn = NULL,
};

/* ---- Stage ①: load a source file fully into an allocator buffer ---- */

int driver_load_source(allocator_t *alloc,
                       const char *path,
                       const char **out_data,
                       size_t *out_len) {
  if (!alloc || !path || !out_data || !out_len) return -1;

  *out_data = NULL;
  *out_len = 0;

  FILE *fp = fopen(path, "rb");
  if (!fp) return -1;

  /* Determine the file size via a seek to the end. */
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return -1;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }

  /* stream_source_mem requires a non-NULL data pointer, so allocate at
   * least one byte even for an empty file. */
  size_t n = (size_t)size;
  char *buf = (char *)allocator_new(alloc, &g_bytes_class, n > 0 ? n : 1);
  if (!buf) {
    fclose(fp);
    return -1;
  }

  size_t read = fread(buf, 1, n, fp);
  fclose(fp);

  if (read != n) {
    /* Short read: the buffer is owned by the allocator; the caller is
     * expected to hand it to a mechanism that frees it (e.g. an
     * owns_data memory source). Signal failure but leave the buffer
     * allocated so it is not leaked. */
    *out_data = buf;
    *out_len = read;
    return -1;
  }

  *out_data = buf;
  *out_len = n;
  return 0;
}

/* ---- Internal: load + lex into a pool, keeping the lexer alive ---- */

static int driver_lex_into(allocator_t *alloc,
                           const char *path,
                           vec_t **out_pool,
                           lexer_t **out_lexer) {
  const char *data = NULL;
  size_t len = 0;
  if (driver_load_source(alloc, path, &data, &len) != 0) return -1;

  stream_source_t src = stream_source_mem(alloc, data, len, /*owns_data=*/true);
  istream_t *stream = istream_open(alloc, src);
  if (!stream) return -1;

  lexer_t *lexer = lexer_create(alloc, stream, path);
  if (!lexer) {
    istream_close(&stream);
    return -1;
  }

  vec_t *pool = vec_new(alloc, /*owns_element=*/true);
  if (!pool) {
    lexer_close(&lexer);
    return -1;
  }

  for (;;) {
    token_t *t = lexer_next(lexer);
    if (!t) break;
    vec_push(pool, alloc, t);
    if (token_get_kind(t) == TOKEN_TYPE_EOF) break;
  }

  *out_pool = pool;
  *out_lexer = lexer;
  return 0;
}

/* ---- Internal: print token text with control-character escapes ---- */

static void print_escaped_text(const char *text, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)text[i];
    switch (c) {
    case '\n': fputs("\\n", stdout); break;
    case '\r': fputs("\\r", stdout); break;
    case '\t': fputs("\\t", stdout); break;
    case '\\': fputs("\\\\", stdout); break;
    case '"': fputs("\\\"", stdout); break;
    default:
      if (c < 0x20 || c == 0x7F) {
        printf("\\x%02X", c);
      } else {
        fputc((int)c, stdout);
      }
    }
  }
}

/* ---- Internal: emit the token table (one token per line) ---- */

static void print_token_table(const vec_t *pool,
                               const char *path,
                               bool *out_has_error) {
  bool has_error = false;
  size_t n = vec_len(pool);
  for (size_t i = 0; i < n; i++) {
    const token_t *t = (const token_t *)vec_get(pool, i);
    if (!t) continue;

    token_kind_t kind = token_get_kind(t);
    if (kind == TOKEN_TYPE_EOF) continue; /* terminator: not a word */

    const location_t *loc = token_get_location(t);
    size_t text_len = 0;
    const char *text = token_get_text(t, &text_len);

    printf("%s L%zu:%zu-%zu:%zu ",
           token_kind_name(kind),
           loc ? loc->begin.line : 0,
           loc ? loc->begin.column : 0,
           loc ? loc->end.line : 0,
           loc ? loc->end.column : 0);
    print_escaped_text(text, text_len);

    if (kind == TOKEN_TYPE_ERROR) {
      has_error = true;
      const char *msg = token_get_error_message(t);
      printf(" error: %s", msg ? msg : "?");
      fprintf(stderr,
              "%s:%zu:%zu: error: %s\n",
              path,
              loc ? loc->begin.line : 0,
              loc ? loc->begin.column : 0,
              msg ? msg : "?");
    }
    putchar('\n');
  }
  if (out_has_error) *out_has_error = has_error;
}

/* ---- Stage ② + ③: lex and print the token table ---- */

int driver_run_file(const char *path) {
  if (!path) {
    fprintf(stderr, "run: no input file\n");
    return 1;
  }

  allocator_t *alloc = create_allocator(malloc, free);
  if (!alloc) {
    fprintf(stderr, "run: out of memory\n");
    return 1;
  }

  vec_t *pool = NULL;
  lexer_t *lexer = NULL;
  if (driver_lex_into(alloc, path, &pool, &lexer) != 0) {
    fprintf(stderr, "run: cannot open file '%s'\n", path);
    if (lexer) lexer_close(&lexer);
    if (pool) vec_free(alloc, &pool);
    delete_allocator(&alloc);
    return 1;
  }

  bool has_error = false;
  print_token_table(pool, path, &has_error);

  /* The lexer owns the memory source (and thus the source buffer); the
   * token text slices are valid only while the lexer is alive, so we
   * print first, then close. */
  lexer_close(&lexer);
  vec_free(alloc, &pool);
  delete_allocator(&alloc);

  return has_error ? 1 : 0;
}

/* ---- Test/utility entry: lex into a pool (text dangles after return) ---- */

int driver_lex_file(allocator_t *alloc, const char *path, vec_t **out_pool) {
  lexer_t *lexer = NULL;
  if (driver_lex_into(alloc, path, out_pool, &lexer) != 0) return -1;
  /* Closing the lexer frees the source buffer; token kinds/locations stay
   * valid but token text slices become dangling. Consumers needing the
   * text should keep the lexer alive (see driver_run_file). */
  lexer_close(&lexer);
  return 0;
}
