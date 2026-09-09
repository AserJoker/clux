#include "diag/diagnostic.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- Internal buffer layout ---- */

struct _diag_buf_t {
  allocator_t   *alloc;
  diagnostic_t  *items; /* dynamic array, grown geometrically */
  size_t         count;
  size_t         capacity;
};

static const char *diag_level_name(diag_level_t level) {
  switch (level) {
    case DIAG_ERROR: return "error";
    case DIAG_WARNING: return "warning";
    case DIAG_NOTE: return "note";
  }
  return "unknown";
}

/* ---- Growth ---- */

static void diag_grow(diag_buf_t *db) {
  size_t new_cap = db->capacity ? db->capacity * 2 : 16;
  /* The array owns only the diagnostic structs; messages are freed
     individually in diag_buf_destroy / on reallocation by hand. */
  diagnostic_t *items = allocator_new_ex(db->alloc, "diagnostic_t",
                                         sizeof(diagnostic_t), NULL, NULL,
                                         NULL, new_cap);
  if (db->items) {
    /* Ownership transfer of structs (incl. message pointers): move, not clone. */
    memcpy(items, db->items, db->count * sizeof(diagnostic_t));
    allocator_free(db->alloc, (void **)&db->items);
  }
  db->items = items;
  db->capacity = new_cap;
}

/* ---- Lifecycle ---- */

diag_buf_t *diag_buf_new(allocator_t *alloc) {
  if (!alloc) return NULL;
  diag_buf_t *db =
      allocator_new_ex(alloc, "diag_buf_t", sizeof(diag_buf_t), NULL, NULL,
                       NULL, 1);
  db->alloc = alloc;
  db->items = NULL;
  db->count = 0;
  db->capacity = 0;
  return db;
}

void diag_buf_destroy(diag_buf_t **db) {
  if (!db || !*db) return;
  diag_buf_t *self = *db;
  for (size_t i = 0; i < self->count; i++) {
    allocator_free(self->alloc, (void **)&self->items[i].message);
  }
  allocator_free(self->alloc, (void **)&self->items);
  allocator_free(self->alloc, (void **)db);
}

/* ---- Recording ---- */

static void diag_add(diag_buf_t *db, diag_level_t level, location_t loc,
                     const char *fmt, va_list ap) {
  if (!db) return;
  if (db->count == db->capacity) diag_grow(db);
  diagnostic_t *d = &db->items[db->count];
  d->level = level;
  d->loc = loc;
  d->message = NULL;
  if (fmt) {
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len >= 0) {
      size_t cap = (size_t)len + 1;
      d->message = allocator_new_ex(db->alloc, "char", sizeof(char),
                                    default_move, default_clone, NULL, cap);
      vsnprintf(d->message, cap, fmt, ap);
    }
  }
  db->count++;
}

void diag_error(diag_buf_t *db, location_t loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_add(db, DIAG_ERROR, loc, fmt, ap);
  va_end(ap);
}

void diag_warning(diag_buf_t *db, location_t loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_add(db, DIAG_WARNING, loc, fmt, ap);
  va_end(ap);
}

void diag_note(diag_buf_t *db, location_t loc, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  diag_add(db, DIAG_NOTE, loc, fmt, ap);
  va_end(ap);
}

/* ---- Queries / output ---- */

size_t diag_count(const diag_buf_t *db) { return db ? db->count : 0; }

bool diag_has_error(const diag_buf_t *db) {
  if (!db) return false;
  for (size_t i = 0; i < db->count; i++) {
    if (db->items[i].level == DIAG_ERROR) return true;
  }
  return false;
}

void diag_print_all(const diag_buf_t *db) {
  if (!db) return;
  for (size_t i = 0; i < db->count; i++) {
    const diagnostic_t *d = &db->items[i];
    const char *file = d->loc.filename ? d->loc.filename : "<unknown>";
    fprintf(stderr, "%s:%zu:%zu: %s: %s\n", file, d->loc.begin.line,
            d->loc.begin.column, diag_level_name(d->level),
            d->message ? d->message : "");
  }
}

const diagnostic_t *diag_items(const diag_buf_t *db) {
  if (!db || db->count == 0) return NULL;
  return db->items;
}
