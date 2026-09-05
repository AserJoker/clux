#include "core/stream.h"
#include "core/panic.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unicode/ubrk.h"
#include "unicode/uchar.h"
#include "unicode/utf8.h"
#include "unicode/ustring.h"
#include "unicode/utypes.h"

/* ================================================================ */
/* Internal helpers                                                  */
/* ================================================================ */

static class_t buf_class = {
    .name = "stream_buf",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

static class_t istream_class = {
    .name = "istream_t",
    .size = 0,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

static class_t ostream_class = {
    .name = "ostream_t",
    .size = 0,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- Line break detection ---- */

static bool is_line_break(UChar32 cp) {
  return cp == 0x000A     /* LF */
         || cp == 0x000D  /* CR */
         || cp == 0x2028  /* Line Separator */
         || cp == 0x2029; /* Paragraph Separator */
}

/* ---- Compute cluster_col for current line using ICU break iterator ---- */

static size_t compute_cluster_col(const char *line_start,
                                  size_t line_len,
                                  size_t pos_offset) {
  if (pos_offset == 0) return 1;

  UErrorCode status = U_ZERO_ERROR;
  int32_t u16_len = 0;
  u_strFromUTF8(NULL, 0, &u16_len, line_start, (int32_t)pos_offset, &status);
  if (U_FAILURE(status) && status != U_BUFFER_OVERFLOW_ERROR) return 1;

  if (u16_len == 0) return 1;

  status = U_ZERO_ERROR;
  UChar *u16_buf = (UChar *)malloc(sizeof(UChar) * (u16_len + 1));
  if (!u16_buf) return 1;
  u_strFromUTF8(
      u16_buf, u16_len + 1, NULL, line_start, (int32_t)pos_offset, &status);
  if (U_FAILURE(status)) {
    free(u16_buf);
    return 1;
  }

  UBreakIterator *bi =
      ubrk_open(UBRK_CHARACTER, NULL, u16_buf, u16_len, &status);
  if (U_FAILURE(status)) {
    free(u16_buf);
    return 1;
  }

  size_t cluster_count = 1;
  int32_t boundary = ubrk_first(bi);
  while (boundary != UBRK_DONE) {
    if (boundary > 0 && boundary <= u16_len) cluster_count++;
    if (boundary >= u16_len) break;
    boundary = ubrk_next(bi);
  }

  ubrk_close(bi);
  free(u16_buf);
  return cluster_count;
}

/* ---- Read a UTF-8 codepoint from a source ---- */

/**
 * Read a single codepoint from the source at the given offset.
 * Writes the codepoint bytes into `seq` (up to 4 bytes).
 * Sets `out_cp_bytes` to the number of bytes consumed.
 * Returns the decoded codepoint, or -1 on EOF.
 */
static UChar32 source_read_cp(stream_source_t *src,
                              size_t offset,
                              char seq[4],
                              size_t *out_cp_bytes) {
  /* Seek to the offset */
  if (src->seek(src->ctx, offset) != 0) return -1;

  /* Read lead byte */
  size_t n = src->read(src->ctx, seq, 1);
  if (n == 0) return -1;

  /* Determine sequence length */
  int seq_len = 1;
  uint8_t lead = (uint8_t)seq[0];
  if (lead >= 0xC2 && lead < 0xE0)
    seq_len = 2;
  else if (lead >= 0xE0 && lead < 0xF0)
    seq_len = 3;
  else if (lead >= 0xF0)
    seq_len = 4;

  /* Read continuation bytes */
  if (seq_len > 1) {
    size_t got = src->read(src->ctx, seq + 1, (size_t)(seq_len - 1));
    if (got < (size_t)(seq_len - 1)) seq_len = 1 + (int)got; /* truncated */
  }

  /* Decode */
  UChar32 cp;
  int32_t pos32 = 0;
  U8_NEXT_OR_FFFD(seq, pos32, seq_len, cp);
  *out_cp_bytes = (size_t)pos32;
  return cp;
}

/* ================================================================ */
/* Built-in: memory source                                           */
/* ================================================================ */

typedef struct {
  const char *data;
  size_t len;
  size_t pos;
  bool owns_data;
  allocator_t *alloc;
} mem_source_ctx_t;

static size_t mem_source_read(void *ctx, void *buf, size_t len) {
  mem_source_ctx_t *c = (mem_source_ctx_t *)ctx;
  size_t avail = c->len - c->pos;
  if (len > avail) len = avail;
  if (len > 0) {
    memcpy(buf, c->data + c->pos, len);
    c->pos += len;
  }
  return len;
}

static int mem_source_seek(void *ctx, size_t offset) {
  mem_source_ctx_t *c = (mem_source_ctx_t *)ctx;
  if (offset > c->len) offset = c->len;
  c->pos = offset;
  return 0;
}

static size_t mem_source_tell(const void *ctx) {
  return ((const mem_source_ctx_t *)ctx)->pos;
}

static const char *mem_source_data(const void *ctx) {
  return ((const mem_source_ctx_t *)ctx)->data;
}

static size_t mem_source_size(const void *ctx) {
  return ((const mem_source_ctx_t *)ctx)->len;
}

static void mem_source_close(void *ctx) {
  if (!ctx) return;
  mem_source_ctx_t *c = (mem_source_ctx_t *)ctx;
  allocator_t *alloc = c->alloc;
  if (c->owns_data) {
    void *b = (void *)c->data;
    allocator_free(alloc, &b);
  }
  allocator_free(alloc, &ctx);
}

stream_source_t stream_source_mem(allocator_t *allocator,
                                  const char *data,
                                  size_t len,
                                  bool owns_data) {
  if (!allocator || !data) return (stream_source_t){0};

  mem_source_ctx_t *ctx = (mem_source_ctx_t *)allocator_new(
      allocator, &buf_class, sizeof(mem_source_ctx_t));
  if (!ctx) return (stream_source_t){0};

  ctx->data = data;
  ctx->len = len;
  ctx->pos = 0;
  ctx->owns_data = owns_data;
  ctx->alloc = allocator;

  return (stream_source_t){
      .ctx = ctx,
      .read = mem_source_read,
      .seek = mem_source_seek,
      .tell = mem_source_tell,
      .data = mem_source_data,
      .size = mem_source_size,
      .close = mem_source_close,
  };
}

/* ================================================================ */
/* Built-in: memory sink                                             */
/* ================================================================ */

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
  allocator_t *alloc;
} mem_sink_ctx_t;

static size_t mem_sink_write(void *ctx, const void *data, size_t len) {
  mem_sink_ctx_t *c = (mem_sink_ctx_t *)ctx;
  /* Grow buffer if needed */
  if (c->len + len > c->cap) {
    size_t new_cap = c->cap ? c->cap : 64;
    while (new_cap < c->len + len)
      new_cap *= 2;
    char *new_buf = (char *)allocator_new(c->alloc, &buf_class, new_cap);
    if (c->buf) memcpy(new_buf, c->buf, c->len);
    if (c->buf) allocator_free(c->alloc, (void **)&c->buf);
    c->buf = new_buf;
    c->cap = new_cap;
  }
  memcpy(c->buf + c->len, data, len);
  c->len += len;
  return len;
}

static int mem_sink_seek(void *ctx, size_t offset) {
  mem_sink_ctx_t *c = (mem_sink_ctx_t *)ctx;
  if (offset > c->len) return -1;
  c->len = offset;
  return 0;
}

static size_t mem_sink_tell(const void *ctx) {
  return ((const mem_sink_ctx_t *)ctx)->len;
}

static const char *mem_sink_data(const void *ctx) {
  return ((const mem_sink_ctx_t *)ctx)->buf;
}

static void mem_sink_reset(void *ctx) {
  mem_sink_ctx_t *c = (mem_sink_ctx_t *)ctx;
  c->len = 0;
}

static void mem_sink_close(void *ctx) {
  if (!ctx) return;
  mem_sink_ctx_t *c = (mem_sink_ctx_t *)ctx;
  allocator_t *alloc = c->alloc;
  if (c->buf) allocator_free(alloc, (void **)&c->buf);
  allocator_free(alloc, &ctx);
}

stream_sink_t stream_sink_mem(allocator_t *allocator) {
  if (!allocator) return (stream_sink_t){0};

  mem_sink_ctx_t *ctx = (mem_sink_ctx_t *)allocator_new(
      allocator, &buf_class, sizeof(mem_sink_ctx_t));
  if (!ctx) return (stream_sink_t){0};

  ctx->buf = NULL;
  ctx->len = 0;
  ctx->cap = 0;
  ctx->alloc = allocator;

  return (stream_sink_t){
      .ctx = ctx,
      .write = mem_sink_write,
      .seek = mem_sink_seek,
      .tell = mem_sink_tell,
      .data = mem_sink_data,
      .reset = mem_sink_reset,
      .close = mem_sink_close,
  };
}

const char *stream_sink_mem_data(const stream_sink_t *sink) {
  if (!sink || !sink->ctx || !sink->data) return NULL;
  return sink->data(sink->ctx);
}

size_t stream_sink_mem_size(const stream_sink_t *sink) {
  if (!sink || !sink->ctx || !sink->tell) return 0;
  return sink->tell(sink->ctx);
}

/* ================================================================ */
/* Built-in: file source                                             */
/* ================================================================ */

typedef struct {
  FILE *fp;
  size_t file_size;
  bool owns_fp;
  allocator_t *alloc;
} file_source_ctx_t;

static size_t file_source_read(void *ctx, void *buf, size_t len) {
  file_source_ctx_t *c = (file_source_ctx_t *)ctx;
  return fread(buf, 1, len, c->fp);
}

static int file_source_seek(void *ctx, size_t offset) {
  file_source_ctx_t *c = (file_source_ctx_t *)ctx;
  if (fseek(c->fp, (long)offset, SEEK_SET) != 0) return -1;
  return 0;
}

static size_t file_source_tell(const void *ctx) {
  file_source_ctx_t *c = (file_source_ctx_t *)ctx;
  return (size_t)ftell(c->fp);
}

static const char *file_source_data(const void *ctx) {
  (void)ctx;
  return NULL; /* File source does not support direct access */
}

static size_t file_source_size(const void *ctx) {
  return ((const file_source_ctx_t *)ctx)->file_size;
}

static void file_source_close(void *ctx) {
  if (!ctx) return;
  file_source_ctx_t *c = (file_source_ctx_t *)ctx;
  allocator_t *alloc = c->alloc;
  if (c->owns_fp && c->fp) fclose(c->fp);
  allocator_free(alloc, &ctx);
}

static stream_source_t
file_source_make(allocator_t *allocator, FILE *fp, bool owns_fp) {
  file_source_ctx_t *ctx = (file_source_ctx_t *)allocator_new(
      allocator, &buf_class, sizeof(file_source_ctx_t));
  if (!ctx) {
    if (owns_fp && fp) fclose(fp);
    return (stream_source_t){0};
  }

  /* Determine file size */
  long saved_pos = ftell(fp);
  size_t fsize = 0;
  if (fseek(fp, 0, SEEK_END) == 0) {
    long end = ftell(fp);
    if (end >= 0) fsize = (size_t)end;
    fseek(fp, saved_pos, SEEK_SET);
  }

  ctx->fp = fp;
  ctx->file_size = fsize;
  ctx->owns_fp = owns_fp;
  ctx->alloc = allocator;

  return (stream_source_t){
      .ctx = ctx,
      .read = file_source_read,
      .seek = file_source_seek,
      .tell = file_source_tell,
      .data = file_source_data,
      .size = file_source_size,
      .close = file_source_close,
  };
}

stream_source_t stream_source_file(allocator_t *allocator, const char *path) {
  if (!allocator || !path) return (stream_source_t){0};

  FILE *fp = fopen(path, "rb");
  if (!fp) return (stream_source_t){0};

  return file_source_make(allocator, fp, true);
}

stream_source_t
stream_source_file_fp(allocator_t *allocator, FILE *fp, bool owns_fp) {
  if (!allocator || !fp) return (stream_source_t){0};
  return file_source_make(allocator, fp, owns_fp);
}

/* ================================================================ */
/* Built-in: file sink                                               */
/* ================================================================ */

typedef struct {
  FILE *fp;
  bool owns_fp;
  allocator_t *alloc;
} file_sink_ctx_t;

static size_t file_sink_write(void *ctx, const void *data, size_t len) {
  file_sink_ctx_t *c = (file_sink_ctx_t *)ctx;
  return fwrite(data, 1, len, c->fp);
}

static int file_sink_seek(void *ctx, size_t offset) {
  file_sink_ctx_t *c = (file_sink_ctx_t *)ctx;
  if (fseek(c->fp, (long)offset, SEEK_SET) != 0) return -1;
  return 0;
}

static size_t file_sink_tell(const void *ctx) {
  file_sink_ctx_t *c = (file_sink_ctx_t *)ctx;
  return (size_t)ftell(c->fp);
}

static const char *file_sink_data(const void *ctx) {
  (void)ctx;
  return NULL; /* File sink does not support direct access */
}

static void file_sink_reset(void *ctx) {
  file_sink_ctx_t *c = (file_sink_ctx_t *)ctx;
  /* Seek to beginning. The file content beyond the new write position
     will remain until overwritten. For true truncation, close and
     reopen the sink. */
  fseek(c->fp, 0, SEEK_SET);
}

static void file_sink_close(void *ctx) {
  if (!ctx) return;
  file_sink_ctx_t *c = (file_sink_ctx_t *)ctx;
  allocator_t *alloc = c->alloc;
  if (c->owns_fp && c->fp) fclose(c->fp);
  allocator_free(alloc, &ctx);
}

static stream_sink_t
file_sink_make(allocator_t *allocator, FILE *fp, bool owns_fp) {
  file_sink_ctx_t *ctx = (file_sink_ctx_t *)allocator_new(
      allocator, &buf_class, sizeof(file_sink_ctx_t));
  if (!ctx) {
    if (owns_fp && fp) fclose(fp);
    return (stream_sink_t){0};
  }

  ctx->fp = fp;
  ctx->owns_fp = owns_fp;
  ctx->alloc = allocator;

  return (stream_sink_t){
      .ctx = ctx,
      .write = file_sink_write,
      .seek = file_sink_seek,
      .tell = file_sink_tell,
      .data = file_sink_data,
      .reset = file_sink_reset,
      .close = file_sink_close,
  };
}

stream_sink_t stream_sink_file(allocator_t *allocator, const char *path) {
  if (!allocator || !path) return (stream_sink_t){0};

  FILE *fp = fopen(path, "wb");
  if (!fp) return (stream_sink_t){0};

  return file_sink_make(allocator, fp, true);
}

stream_sink_t
stream_sink_file_fp(allocator_t *allocator, FILE *fp, bool owns_fp) {
  if (!allocator || !fp) return (stream_sink_t){0};
  return file_sink_make(allocator, fp, owns_fp);
}

/* ================================================================ */
/* istream_t                                                         */
/* ================================================================ */

struct _istream_t {
  stream_source_t source;
  allocator_t *allocator;

  /* Line buffer: holds bytes from current line_start to current pos.
     Used for cluster_col computation via ICU break iterator. */
  char *line_buf;
  size_t line_buf_cap;
  size_t line_buf_len;

  /* Position tracking */
  size_t pos;        /* absolute byte offset in source */
  size_t line;       /* 1-based */
  size_t col;        /* 1-based codepoint column */
  size_t line_start; /* absolute byte offset of current line start */
};

/* ---- Ensure line buffer capacity ---- */

static void istream_ensure_line_buf(istream_t *s, size_t extra) {
  if (s->line_buf_len + extra <= s->line_buf_cap) return;
  size_t new_cap = s->line_buf_cap ? s->line_buf_cap * 2 : 64;
  while (new_cap < s->line_buf_len + extra)
    new_cap *= 2;
  char *new_buf = (char *)allocator_new(s->allocator, &buf_class, new_cap);
  if (s->line_buf) memcpy(new_buf, s->line_buf, s->line_buf_len);
  if (s->line_buf) allocator_free(s->allocator, (void **)&s->line_buf);
  s->line_buf = new_buf;
  s->line_buf_cap = new_cap;
}

istream_t *istream_open(allocator_t *allocator, stream_source_t source) {
  if (!allocator || !source.ctx) return NULL;

  istream_class.size = sizeof(istream_t);
  istream_t *s = (istream_t *)allocator_new(allocator, &istream_class, 1);
  s->source = source;
  s->allocator = allocator;
  s->line_buf = NULL;
  s->line_buf_cap = 0;
  s->line_buf_len = 0;
  s->pos = 0;
  s->line = 1;
  s->col = 1;
  s->line_start = 0;
  return s;
}

void istream_close(istream_t **stream) {
  if (!stream || !*stream) return;
  istream_t *s = *stream;
  allocator_t *alloc = s->allocator;

  /* Close the source backend */
  if (s->source.close) s->source.close(s->source.ctx);

  /* Free line buffer */
  if (s->line_buf) allocator_free(alloc, (void **)&s->line_buf);

  allocator_free(alloc, (void **)stream);
}

UChar32 istream_read_cp(istream_t *stream) {
  if (!stream) return -1;

  /* Read codepoint from source */
  char seq[4] = {0};
  size_t cp_bytes = 0;
  UChar32 cp = source_read_cp(&stream->source, stream->pos, seq, &cp_bytes);
  if (cp == -1) return -1;

  /* Append to line buffer */
  istream_ensure_line_buf(stream, cp_bytes);
  memcpy(stream->line_buf + stream->line_buf_len, seq, cp_bytes);
  stream->line_buf_len += cp_bytes;

  /* Update position tracking */
  if (is_line_break(cp)) {
    stream->line++;
    stream->col = 1;
    /* CR+LF: treat as single line break */
    if (cp == 0x000D) {
      char next_seq[4] = {0};
      size_t next_bytes = 0;
      UChar32 next_cp = source_read_cp(
          &stream->source, stream->pos + cp_bytes, next_seq, &next_bytes);
      if (next_cp == 0x000A) {
        /* Consume the LF as well */
        cp_bytes += next_bytes;
      } else {
        /* Not LF — seek back */
        stream->source.seek(stream->source.ctx, stream->pos + cp_bytes);
      }
    }
    stream->line_start = stream->pos + cp_bytes;
    /* Reset line buffer for new line */
    stream->line_buf_len = 0;
  } else {
    stream->col++;
  }

  stream->pos += cp_bytes;
  return cp;
}

UChar32 istream_peek_cp(istream_t *stream) {
  if (!stream) return -1;

  char seq[4] = {0};
  size_t cp_bytes = 0;
  UChar32 cp = source_read_cp(&stream->source, stream->pos, seq, &cp_bytes);

  /* Restore source position */
  if (cp_bytes > 0) stream->source.seek(stream->source.ctx, stream->pos);

  return cp;
}

stream_pos_t istream_tell(const istream_t *stream) {
  stream_pos_t pos = {0, 1, 1, 1};
  if (!stream) return pos;
  pos.byte_offset = stream->pos;
  pos.line = stream->line;
  pos.col = stream->col;
  /* Compute cluster_col from line buffer */
  if (stream->line_buf && stream->line_buf_len > 0) {
    pos.cluster_col = compute_cluster_col(
        stream->line_buf, stream->line_buf_len, stream->line_buf_len);
  }
  return pos;
}

/* ---- Scan from byte 0 to target to rebuild line/col/line_start ---- */

typedef struct {
  size_t line;
  size_t col;
  size_t line_start;
} pos_info_t;

static pos_info_t scan_position(stream_source_t *src, size_t target_offset) {
  pos_info_t info = {1, 1, 0};

  if (target_offset == 0) return info;

  /* Seek to beginning */
  if (src->seek(src->ctx, 0) != 0) return info;

  size_t byte_pos = 0;
  while (byte_pos < target_offset) {
    char seq[4] = {0};
    size_t cp_bytes = 0;
    UChar32 cp = source_read_cp(src, byte_pos, seq, &cp_bytes);
    if (cp == -1 || cp_bytes == 0) break;

    if (is_line_break(cp)) {
      info.line++;
      info.col = 1;
      size_t next_off = byte_pos + cp_bytes;
      if (cp == 0x000D && next_off < target_offset) {
        char next_seq[4] = {0};
        size_t next_bytes = 0;
        UChar32 next_cp = source_read_cp(src, next_off, next_seq, &next_bytes);
        if (next_cp == 0x000A) {
          info.line_start = next_off + next_bytes;
          byte_pos = next_off + next_bytes;
          continue;
        }
      }
      info.line_start = byte_pos + cp_bytes;
    } else {
      info.col++;
    }
    byte_pos += cp_bytes;
  }

  return info;
}

void istream_seek(istream_t *stream, size_t byte_offset) {
  if (!stream) return;

  size_t total = 0;
  if (stream->source.size) total = stream->source.size(stream->source.ctx);
  if (byte_offset > total) byte_offset = total;

  if (byte_offset == 0) {
    stream->pos = 0;
    stream->line = 1;
    stream->col = 1;
    stream->line_start = 0;
    stream->line_buf_len = 0;
    stream->source.seek(stream->source.ctx, 0);
    return;
  }

  pos_info_t info = scan_position(&stream->source, byte_offset);
  stream->pos = byte_offset;
  stream->line = info.line;
  stream->col = info.col;
  stream->line_start = info.line_start;

  /* Rebuild line buffer from line_start to byte_offset */
  size_t line_len = byte_offset - info.line_start;
  if (line_len > 0) {
    istream_ensure_line_buf(stream, line_len);
    stream->source.seek(stream->source.ctx, info.line_start);
    size_t n =
        stream->source.read(stream->source.ctx, stream->line_buf, line_len);
    stream->line_buf_len = n;
  } else {
    stream->line_buf_len = 0;
  }

  /* Restore source position to byte_offset */
  stream->source.seek(stream->source.ctx, byte_offset);
}

int istream_scanf(istream_t *stream, const char *fmt, ...) {
  if (!stream || !fmt) return EOF;

  size_t total =
      stream->source.size ? stream->source.size(stream->source.ctx) : 0;
  size_t remaining = (stream->pos < total) ? total - stream->pos : 0;
  if (remaining == 0) return EOF;

  /* Try direct access first (memory source) */
  const char *start = NULL;
  bool needs_free = false;

  if (stream->source.data) {
    const char *src_data = stream->source.data(stream->source.ctx);
    if (src_data) start = src_data + stream->pos;
  }

  if (!start) {
    /* Fall back: read remaining data into a temp buffer */
    char *tmp =
        (char *)allocator_new(stream->allocator, &buf_class, remaining + 1);
    if (!tmp) return EOF;
    stream->source.seek(stream->source.ctx, stream->pos);
    size_t n = stream->source.read(stream->source.ctx, tmp, remaining);
    tmp[n] = '\0';
    start = tmp;
    needs_free = true;
  }

  va_list ap;
  va_start(ap, fmt);
  int result = vsscanf(start, fmt, ap);
  va_end(ap);

  if (needs_free) {
    char *tmp = (char *)start;
    allocator_free(stream->allocator, (void **)&tmp);
  }

  return result;
}

const char *istream_data(const istream_t *stream) {
  if (!stream || !stream->source.data) return NULL;
  return stream->source.data(stream->source.ctx);
}

size_t istream_size(const istream_t *stream) {
  if (!stream || !stream->source.size) return 0;
  return stream->source.size(stream->source.ctx);
}

size_t istream_remaining(const istream_t *stream) {
  if (!stream) return 0;
  size_t total =
      stream->source.size ? stream->source.size(stream->source.ctx) : 0;
  return (stream->pos < total) ? total - stream->pos : 0;
}

bool istream_at_end(const istream_t *stream) {
  if (!stream) return true;
  size_t total =
      stream->source.size ? stream->source.size(stream->source.ctx) : 0;
  return stream->pos >= total;
}

/* ================================================================ */
/* ostream_t                                                         */
/* ================================================================ */

struct _ostream_t {
  stream_sink_t sink;
  allocator_t *allocator;

  /* Position tracking */
  size_t line;       /* 1-based */
  size_t col;        /* 1-based codepoint column */
  size_t line_start; /* absolute byte offset of current line start */

  /* Line buffer for cluster_col computation */
  char *line_buf;
  size_t line_buf_cap;
  size_t line_buf_len;
};

/* ---- Ensure line buffer capacity ---- */

static void ostream_ensure_line_buf(ostream_t *s, size_t extra) {
  if (s->line_buf_len + extra <= s->line_buf_cap) return;
  size_t new_cap = s->line_buf_cap ? s->line_buf_cap * 2 : 64;
  while (new_cap < s->line_buf_len + extra)
    new_cap *= 2;
  char *new_buf = (char *)allocator_new(s->allocator, &buf_class, new_cap);
  if (s->line_buf) memcpy(new_buf, s->line_buf, s->line_buf_len);
  if (s->line_buf) allocator_free(s->allocator, (void **)&s->line_buf);
  s->line_buf = new_buf;
  s->line_buf_cap = new_cap;
}

/* ---- Update position tracking after writing data ---- */

static void ostream_update_pos(ostream_t *s,
                               const char *data,
                               size_t len,
                               size_t write_start) {
  /* Track the offset of the last line break within this write chunk.
     Bytes after the last line break belong to the current line and
     should be in the line buffer. */
  size_t last_line_break_end = (size_t)-1; /* offset within data */

  size_t i = 0;
  while (i < len) {
    UChar32 cp;
    int32_t i32 = (int32_t)i;
    U8_NEXT_OR_FFFD(data, i32, (int32_t)len, cp);

    if (is_line_break(cp)) {
      s->line++;
      s->col = 1;
      /* CR+LF */
      if (cp == 0x000D && (size_t)i32 < len) {
        UChar32 next;
        int32_t next_i = i32;
        U8_NEXT(data, next_i, (int32_t)len, next);
        if (next == 0x000A) {
          s->line_start = write_start + (size_t)next_i;
          last_line_break_end = (size_t)next_i;
          i = (size_t)next_i;
          continue;
        }
      }
      s->line_start = write_start + (size_t)i32;
      last_line_break_end = (size_t)i32;
    } else {
      s->col++;
    }
    i = (size_t)i32;
  }

  /* Update line buffer: if there was a line break, discard old content
     and only keep bytes after the last line break.
     Otherwise, append all written bytes. */
  if (last_line_break_end != (size_t)-1) {
    /* Line break occurred — reset line buffer and add only the tail */
    s->line_buf_len = 0;
    size_t tail_len = len - last_line_break_end;
    if (tail_len > 0) {
      ostream_ensure_line_buf(s, tail_len);
      memcpy(s->line_buf, data + last_line_break_end, tail_len);
      s->line_buf_len = tail_len;
    }
  } else {
    /* No line break — append all bytes to line buffer */
    ostream_ensure_line_buf(s, len);
    memcpy(s->line_buf + s->line_buf_len, data, len);
    s->line_buf_len += len;
  }
}

ostream_t *ostream_open(allocator_t *allocator, stream_sink_t sink) {
  if (!allocator || !sink.ctx) return NULL;

  ostream_class.size = sizeof(ostream_t);
  ostream_t *s = (ostream_t *)allocator_new(allocator, &ostream_class, 1);
  s->sink = sink;
  s->allocator = allocator;
  s->line = 1;
  s->col = 1;
  s->line_start = 0;
  s->line_buf = NULL;
  s->line_buf_cap = 0;
  s->line_buf_len = 0;
  return s;
}

void ostream_close(ostream_t **stream) {
  if (!stream || !*stream) return;
  ostream_t *s = *stream;
  allocator_t *alloc = s->allocator;

  if (s->sink.close) s->sink.close(s->sink.ctx);

  if (s->line_buf) allocator_free(alloc, (void **)&s->line_buf);

  allocator_free(alloc, (void **)stream);
}

void ostream_write_cp(ostream_t *stream, UChar32 cp) {
  if (!stream) return;

  char buf[4];
  int32_t pos32 = 0;
  UBool isError = false;
  U8_APPEND(buf, pos32, 4, cp, isError);
  if (isError) return;

  size_t cp_bytes = (size_t)pos32;
  size_t write_start = stream->sink.tell(stream->sink.ctx);
  stream->sink.write(stream->sink.ctx, buf, cp_bytes);

  /* Append to line buffer */
  ostream_ensure_line_buf(stream, cp_bytes);
  memcpy(stream->line_buf + stream->line_buf_len, buf, cp_bytes);
  stream->line_buf_len += cp_bytes;

  if (is_line_break(cp)) {
    stream->line++;
    stream->col = 1;
    stream->line_start = write_start + cp_bytes;
    stream->line_buf_len = 0;
  } else {
    stream->col++;
  }
}

void ostream_write(ostream_t *stream, const char *data, size_t len) {
  if (!stream || !data || len == 0) return;

  size_t write_start = stream->sink.tell(stream->sink.ctx);
  stream->sink.write(stream->sink.ctx, data, len);
  ostream_update_pos(stream, data, len, write_start);
}

stream_pos_t ostream_tell(const ostream_t *stream) {
  stream_pos_t pos = {0, 1, 1, 1};
  if (!stream) return pos;
  pos.byte_offset = stream->sink.tell(stream->sink.ctx);
  pos.line = stream->line;
  pos.col = stream->col;
  if (stream->line_buf && stream->line_buf_len > 0) {
    pos.cluster_col = compute_cluster_col(
        stream->line_buf, stream->line_buf_len, stream->line_buf_len);
  }
  return pos;
}

int ostream_printf(ostream_t *stream, const char *fmt, ...) {
  if (!stream || !fmt) return 0;

  va_list ap;
  va_start(ap, fmt);
  int needed = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);

  if (needed <= 0) return 0;

  char *tmp =
      (char *)allocator_new(stream->allocator, &buf_class, (size_t)needed + 1);
  if (!tmp) return 0;

  va_start(ap, fmt);
  int written = vsnprintf(tmp, (size_t)needed + 1, fmt, ap);
  va_end(ap);

  if (written <= 0) {
    allocator_free(stream->allocator, (void **)&tmp);
    return 0;
  }

  size_t write_len = (size_t)written;
  ostream_write(stream, tmp, write_len);
  allocator_free(stream->allocator, (void **)&tmp);
  return written;
}

const char *ostream_data(const ostream_t *stream) {
  if (!stream || !stream->sink.data) return NULL;
  return stream->sink.data(stream->sink.ctx);
}

size_t ostream_size(const ostream_t *stream) {
  if (!stream) return 0;
  return stream->sink.tell(stream->sink.ctx);
}

void ostream_reset(ostream_t *stream) {
  if (!stream) return;
  if (stream->sink.reset) stream->sink.reset(stream->sink.ctx);
  stream->line = 1;
  stream->col = 1;
  stream->line_start = 0;
  stream->line_buf_len = 0;
}
