#ifndef _H_CLUX_CORE_STREAM_
#define _H_CLUX_CORE_STREAM_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "unicode/utypes.h"

/* ---- Position info ---- */

/**
 * Position in a stream, returned by istream_tell / ostream_tell.
 * All fields are 1-based for line/col, 0-based for byte_offset.
 */
typedef struct {
  size_t byte_offset; /* absolute byte position in buffer */
  size_t line;        /* 1-based line number */
  size_t col;         /* 1-based codepoint column from line start */
  size_t cluster_col; /* 1-based grapheme-cluster column from line start */
} stream_pos_t;

/* ================================================================ */
/* stream_source_t — pluggable read backend                          */
/* ================================================================ */

/**
 * Function table + opaque context for a byte-level read source.
 *
 * All operations are delegated to these callbacks; the stream never
 * accesses the backing store directly.  The source must support
 * random access (seek) for full stream functionality.
 *
 * Lifecycle:
 *   1. Create via a factory (e.g. stream_source_mem).
 *   2. Pass to istream_open.
 *   3. The stream calls close(ctx) on destruction.
 */
typedef struct {
  void *ctx;

  /** Read up to `len` bytes into `buf` from the current position.
   *  Returns the number of bytes actually read.  0 = EOF. */
  size_t (*read)(void *ctx, void *buf, size_t len);

  /** Seek to absolute byte offset.  Returns 0 on success, -1 if unsupported. */
  int (*seek)(void *ctx, size_t offset);

  /** Return the current byte position. */
  size_t (*tell)(const void *ctx);

  /** Return a pointer to the source data, or NULL if not supported.
   *  When non-NULL, the pointer remains valid until close() is called. */
  const char *(*data)(const void *ctx);

  /** Total size of the source data.  Returns 0 if unknown. */
  size_t (*size)(const void *ctx);

  /** Release resources held by ctx. */
  void (*close)(void *ctx);
} stream_source_t;

/* ================================================================ */
/* stream_sink_t — pluggable write backend                           */
/* ================================================================ */

/**
 * Function table + opaque context for a byte-level write sink.
 *
 * Lifecycle:
 *   1. Create via a factory (e.g. stream_sink_mem).
 *   2. Pass to ostream_open.
 *   3. The stream calls close(ctx) on destruction.
 */
typedef struct {
  void *ctx;

  /** Write `len` bytes from `data`.  Returns bytes actually written. */
  size_t (*write)(void *ctx, const void *data, size_t len);

  /** Seek to absolute byte offset.  Returns 0 on success, -1 if unsupported. */
  int (*seek)(void *ctx, size_t offset);

  /** Return the current write position (= total bytes written for append-only). */
  size_t (*tell)(const void *ctx);

  /** Return a pointer to the written data, or NULL if not supported.
   *  When non-NULL, the pointer remains valid until close() or reset() is called. */
  const char *(*data)(const void *ctx);

  /** Reset the sink to its initial state (e.g. clear internal buffer). */
  void (*reset)(void *ctx);

  /** Release resources held by ctx. */
  void (*close)(void *ctx);
} stream_sink_t;

/* ================================================================ */
/* Built-in source/sink factories                                    */
/* ================================================================ */

/**
 * Create a memory-backed source that reads from an existing buffer.
 * If `owns_data` is true, the buffer is freed via `allocator` on close.
 * `data` must outlive the source if `owns_data` is false.
 */
stream_source_t stream_source_mem(allocator_t *allocator,
                                  const char *data,
                                  size_t len,
                                  bool owns_data);

/**
 * Create a memory-backed sink that writes to a growable buffer.
 * The buffer is managed by the sink internally using `allocator`.
 */
stream_sink_t stream_sink_mem(allocator_t *allocator);

/**
 * Return the internal buffer of a memory sink (or NULL).
 * The pointer is valid until the next write/reset/close.
 */
const char *stream_sink_mem_data(const stream_sink_t *sink);

/**
 * Return the number of bytes written to a memory sink.
 */
size_t stream_sink_mem_size(const stream_sink_t *sink);

/* ---- File-backed source/sink ---- */

/**
 * Create a file-backed source that reads from the file at `path`.
 * Opens the file in binary read mode ("rb").
 * The source is seekable; size() returns the file size.
 * data() returns NULL (no direct access).
 * Returns a zero-initialized source on failure.
 */
stream_source_t stream_source_file(allocator_t *allocator, const char *path);

/**
 * Create a file-backed source wrapping an existing FILE*.
 * If `owns_fp` is true, fclose(fp) is called on close.
 * The FILE* must be seekable and opened in binary mode.
 * Returns a zero-initialized source on failure (NULL args).
 */
stream_source_t
stream_source_file_fp(allocator_t *allocator, FILE *fp, bool owns_fp);

/**
 * Create a file-backed sink that writes to the file at `path`.
 * Opens the file in binary write mode ("wb"), creating or truncating.
 * data() returns NULL (no direct access).
 * reset() seeks to the beginning of the file.
 * Returns a zero-initialized sink on failure.
 */
stream_sink_t stream_sink_file(allocator_t *allocator, const char *path);

/**
 * Create a file-backed sink wrapping an existing FILE*.
 * If `owns_fp` is true, fclose(fp) is called on close.
 * data() returns NULL (no direct access).
 * reset() seeks to the beginning of the file.
 * Returns a zero-initialized sink on failure (NULL args).
 */
stream_sink_t
stream_sink_file_fp(allocator_t *allocator, FILE *fp, bool owns_fp);

/* ================================================================ */
/* istream_t — UTF-8 read stream                                     */
/* ================================================================ */

typedef struct _istream_t istream_t;

/* ---- Construction / destruction ---- */

/**
 * Open a read stream over the given source.
 * The stream takes ownership of the source (calls close on destruction).
 * Returns NULL if allocator or source.ctx is NULL.
 */
istream_t *istream_open(allocator_t *allocator, stream_source_t source);

/**
 * Close the read stream and nullify the caller's pointer.
 * Calls source.close(ctx) to release backend resources.
 * No-op if stream or *stream is NULL.
 */
void istream_close(istream_t **stream);

/* ---- Codepoint reading ---- */

/**
 * Read and return the next codepoint at the current position.
 * Returns -1 on EOF. Invalid sequences yield U+FFFD.
 * Advances position and updates line/col/cluster_col.
 */
UChar32 istream_read_cp(istream_t *stream);

/**
 * Peek at the next codepoint without advancing the position.
 * Returns -1 on EOF. Requires a seekable source.
 */
UChar32 istream_peek_cp(istream_t *stream);

/* ---- Position ---- */

/**
 * Return the current position (byte_offset, line, col, cluster_col).
 */
stream_pos_t istream_tell(const istream_t *stream);

/**
 * Seek to an absolute byte offset. Recomputes line/col/cluster_col
 * by scanning from the start (O(n)). Requires a seekable source.
 * Clamps to [0, source_size].
 */
void istream_seek(istream_t *stream, size_t byte_offset);

/* ---- Formatted reading ---- */

/**
 * scanf-style formatted read from the current position.
 * Returns the number of items successfully matched, or EOF.
 * Does not advance past the last consumed character on failure.
 */
int istream_scanf(istream_t *stream, const char *fmt, ...);

/* ---- Buffer info ---- */

/**
 * Return a pointer to the source data, or NULL if the source
 * does not support direct access.
 */
const char *istream_data(const istream_t *stream);

/** Return the total size of the source data. */
size_t istream_size(const istream_t *stream);

/** Return bytes remaining from current position. */
size_t istream_remaining(const istream_t *stream);

/** Return true if the current position is at or past the end. */
bool istream_at_end(const istream_t *stream);

/* ================================================================ */
/* ostream_t — UTF-8 write stream                                    */
/* ================================================================ */

typedef struct _ostream_t ostream_t;

/* ---- Construction / destruction ---- */

/**
 * Open a write stream over the given sink.
 * The stream takes ownership of the sink (calls close on destruction).
 * Returns NULL if allocator or sink.ctx is NULL.
 */
ostream_t *ostream_open(allocator_t *allocator, stream_sink_t sink);

/**
 * Close the write stream and nullify the caller's pointer.
 * Calls sink.close(ctx) to release backend resources.
 * No-op if stream or *stream is NULL.
 */
void ostream_close(ostream_t **stream);

/* ---- Codepoint writing ---- */

/**
 * Write a single codepoint. Updates line/col/cluster_col.
 */
void ostream_write_cp(ostream_t *stream, UChar32 cp);

/**
 * Write raw bytes (caller guarantees valid UTF-8).
 * Updates line/col/cluster_col.
 */
void ostream_write(ostream_t *stream, const char *data, size_t len);

/* ---- Position ---- */

/**
 * Return the current position (byte_offset, line, col, cluster_col).
 */
stream_pos_t ostream_tell(const ostream_t *stream);

/* ---- Formatted writing ---- */

/**
 * printf-style formatted write.
 * Updates line/col/cluster_col.
 * Returns the number of bytes written.
 */
int ostream_printf(ostream_t *stream, const char *fmt, ...);

/* ---- Buffer info ---- */

/**
 * Return a pointer to the written data, or NULL if the sink
 * does not support direct access.
 */
const char *ostream_data(const ostream_t *stream);

/** Return the number of bytes written. */
size_t ostream_size(const ostream_t *stream);

/**
 * Reset to initial state. Calls sink.reset(ctx).
 */
void ostream_reset(ostream_t *stream);

#ifdef __cplusplus
}
#endif
#endif
