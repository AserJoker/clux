#include "core/string.h"
#include "core/panic.h"
#include <stdint.h>
#include <string.h>

/* ---- Internal: string_t definition ---- */

struct _string_t {
  char *data;           /* NUL-terminated buffer (allocator-managed) */
  size_t len;           /* byte length, excluding the NUL terminator */
  size_t cap;           /* allocated bytes (includes the NUL slot) */
  allocator_t *allocator; /* allocator used for all internal allocations */
};

/* ---- Internal: class for the character buffer (size=1) ---- */

static class_t char_class = {
    .name = "char",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- Internal: class for string_t itself ---- */

static void string_dispose(void *self, allocator_t *allocator);
static void string_move_cb(void *self, allocator_t *allocator, void *another);
static void string_clone_cb(void *self, allocator_t *allocator, void *another);

static class_t string_class = {
    .name = "string_t",
    .size = sizeof(string_t),
    .move_fn = string_move_cb,
    .clone_fn = string_clone_cb,
    .dispose_fn = string_dispose,
};

/* ---- Internal: growth policy ---- */

static size_t next_capacity(size_t current, size_t required) {
  if (current == 0)
    return required < 4 ? 4 : required;
  size_t grown = current * 2;
  if (grown < current) /* overflow */
    grown = SIZE_MAX;
  return grown < required ? required : grown;
}

/* ---- Internal: grow the buffer so that cap >= min_cap ---- */

static void string_grow(string_t *str, size_t min_cap) {
  if (min_cap <= str->cap)
    return;
  size_t new_cap = next_capacity(str->cap, min_cap);

  char *new_data = (char *)allocator_new(str->allocator, &char_class, new_cap);
  if (str->len > 0)
    memcpy(new_data, str->data, str->len);
  new_data[str->len] = '\0';

  if (str->data) {
    void *old = str->data;
    allocator_free(str->allocator, &old);
  }
  str->data = new_data;
  str->cap = new_cap;
}

/* ---- Internal: byte-level search (like memmem) ---- */

static size_t mem_search(const char *hay, size_t hay_len, const char *needle,
                         size_t needle_len, size_t start) {
  if (needle_len == 0)
    return start;
  if (needle_len > hay_len || start > hay_len - needle_len)
    return STRING_NPOS;
  for (size_t i = start; i <= hay_len - needle_len; i++) {
    if (hay[i] == needle[0] && memcmp(hay + i, needle, needle_len) == 0)
      return i;
  }
  return STRING_NPOS;
}

/* ---- Construction / destruction ---- */

string_t *string_new(allocator_t *allocator) {
  if (!allocator)
    return NULL;
  string_t *str = (string_t *)allocator_new(allocator, &string_class, 1);
  str->data = NULL;
  str->len = 0;
  str->cap = 0;
  str->allocator = allocator;
  return str;
}

string_t *string_from_cstr(allocator_t *allocator, const char *cstr) {
  if (!allocator || !cstr)
    return NULL;
  return string_from_bytes(allocator, cstr, strlen(cstr));
}

string_t *string_from_bytes(allocator_t *allocator, const char *data,
                            size_t len) {
  if (!allocator || !data)
    return NULL;
  string_t *str = string_new(allocator);
  if (!str)
    return NULL;
  if (len > 0) {
    string_grow(str, len + 1);
    memcpy(str->data, data, len);
    str->len = len;
    str->data[len] = '\0';
  }
  return str;
}

string_t *string_from_string(allocator_t *allocator, const string_t *other) {
  if (!allocator || !other)
    return NULL;
  return string_from_bytes(allocator, other->data ? other->data : "",
                           other->len);
}

void string_free(string_t **str) {
  if (!str || !*str)
    return;
  string_t *s = *str;
  if (s->allocator)
    allocator_free(s->allocator, (void **)str);
  else
    *str = NULL;
}

/* ---- Accessors ---- */

const char *string_cstr(const string_t *str) {
  if (!str || !str->data)
    return "";
  return str->data;
}

const char *string_data(const string_t *str) { return string_cstr(str); }

size_t string_len(const string_t *str) {
  if (!str)
    return 0;
  return str->len;
}

size_t string_cap(const string_t *str) {
  if (!str)
    return 0;
  return str->cap;
}

bool string_is_empty(const string_t *str) {
  if (!str)
    return true;
  return str->len == 0;
}

int string_char_at(const string_t *str, size_t index) {
  if (!str || index >= str->len)
    return -1;
  return (unsigned char)str->data[index];
}

/* ---- Mutation ---- */

void string_append_cstr(string_t *str, const char *cstr) {
  if (!str || !cstr)
    return;
  string_append_bytes(str, cstr, strlen(cstr));
}

void string_append_bytes(string_t *str, const char *data, size_t len) {
  if (!str || !data || len == 0)
    return;
  if (len > SIZE_MAX - str->len - 1)
    panic("string: capacity overflow");
  size_t need = str->len + len + 1;
  if (need > str->cap)
    string_grow(str, need);
  memcpy(str->data + str->len, data, len);
  str->len += len;
  str->data[str->len] = '\0';
}

void string_append_char(string_t *str, char ch) {
  if (!str)
    return;
  string_append_bytes(str, &ch, 1);
}

void string_append_string(string_t *str, const string_t *other) {
  if (!str || !other)
    return;
  if (other->len > 0)
    string_append_bytes(str, other->data, other->len);
}

void string_assign_cstr(string_t *str, const char *cstr) {
  if (!str || !cstr)
    return;
  string_clear(str);
  string_append_cstr(str, cstr);
}

void string_assign_bytes(string_t *str, const char *data, size_t len) {
  if (!str || !data)
    return;
  string_clear(str);
  string_append_bytes(str, data, len);
}

void string_clear(string_t *str) {
  if (!str)
    return;
  str->len = 0;
  if (str->data)
    str->data[0] = '\0';
}

void string_reserve(string_t *str, size_t additional) {
  if (!str || additional == 0)
    return;
  if (additional > SIZE_MAX - str->len - 1)
    panic("string_reserve: capacity overflow");
  size_t need = str->len + additional + 1;
  if (need > str->cap)
    string_grow(str, need);
}

void string_shrink_to_fit(string_t *str) {
  if (!str || str->len + 1 == str->cap)
    return;

  if (str->len == 0) {
    if (str->data) {
      void *old = str->data;
      allocator_free(str->allocator, &old);
    }
    str->data = NULL;
    str->cap = 0;
    return;
  }

  char *new_data =
      (char *)allocator_new(str->allocator, &char_class, str->len + 1);
  memcpy(new_data, str->data, str->len + 1);

  void *old = str->data;
  allocator_free(str->allocator, &old);

  str->data = new_data;
  str->cap = str->len + 1;
}

/* ---- Searching ---- */

size_t string_find(const string_t *str, const char *needle, size_t start) {
  if (!str || !needle)
    return STRING_NPOS;
  size_t nlen = strlen(needle);
  if (nlen == 0)
    return start <= str->len ? start : STRING_NPOS;
  if (start > str->len)
    return STRING_NPOS;
  return mem_search(str->data, str->len, needle, nlen, start);
}

size_t string_rfind(const string_t *str, const char *needle) {
  if (!str || !needle)
    return STRING_NPOS;
  size_t nlen = strlen(needle);
  if (nlen == 0)
    return str->len;
  if (nlen > str->len)
    return STRING_NPOS;
  for (size_t i = str->len - nlen;; i--) {
    if (memcmp(str->data + i, needle, nlen) == 0)
      return i;
    if (i == 0)
      break;
  }
  return STRING_NPOS;
}

bool string_contains(const string_t *str, const char *needle) {
  return string_find(str, needle, 0) != STRING_NPOS;
}

bool string_starts_with(const string_t *str, const char *prefix) {
  if (!str || !prefix)
    return false;
  size_t plen = strlen(prefix);
  if (plen > str->len)
    return false;
  return plen == 0 || memcmp(str->data, prefix, plen) == 0;
}

bool string_ends_with(const string_t *str, const char *suffix) {
  if (!str || !suffix)
    return false;
  size_t slen = strlen(suffix);
  if (slen > str->len)
    return false;
  return slen == 0 || memcmp(str->data + str->len - slen, suffix, slen) == 0;
}

/* ---- Comparison ---- */

int string_compare(const string_t *a, const string_t *b) {
  if (a == b)
    return 0;
  if (!a)
    return -1;
  if (!b)
    return 1;
  size_t min_len = a->len < b->len ? a->len : b->len;
  int r = min_len > 0 ? memcmp(a->data, b->data, min_len) : 0;
  if (r != 0)
    return r;
  return (a->len > b->len) - (a->len < b->len);
}

bool string_equals(const string_t *a, const string_t *b) {
  if (a == b)
    return true;
  if (!a || !b)
    return false;
  return a->len == b->len &&
         (a->len == 0 || memcmp(a->data, b->data, a->len) == 0);
}

/* ---- Derived strings ---- */

string_t *string_substring(allocator_t *allocator, const string_t *str,
                           size_t start, size_t len) {
  if (!allocator || !str)
    return NULL;
  if (start > str->len)
    return NULL;
  size_t avail = str->len - start;
  if (len > avail)
    len = avail;

  string_t *out = string_new(allocator);
  if (!out)
    return NULL;
  if (len > 0) {
    string_grow(out, len + 1);
    memcpy(out->data, str->data + start, len);
    out->len = len;
    out->data[len] = '\0';
  }
  return out;
}

string_t *string_concat(allocator_t *allocator, const string_t *a,
                        const string_t *b) {
  if (!allocator || !a || !b)
    return NULL;
  if (a->len > SIZE_MAX - b->len - 1)
    panic("string_concat: length overflow");

  string_t *out = string_new(allocator);
  if (!out)
    return NULL;
  string_grow(out, a->len + b->len + 1);
  if (a->len > 0)
    memcpy(out->data, a->data, a->len);
  if (b->len > 0)
    memcpy(out->data + a->len, b->data, b->len);
  out->len = a->len + b->len;
  out->data[out->len] = '\0';
  return out;
}

string_t *string_replace(allocator_t *allocator, const string_t *str,
                         const char *needle, const char *replacement) {
  if (!allocator || !str || !needle)
    return NULL;
  if (needle[0] == '\0')
    return string_from_string(allocator, str);

  size_t nlen = strlen(needle);
  size_t rlen = replacement ? strlen(replacement) : 0;
  size_t pos = mem_search(str->data, str->len, needle, nlen, 0);
  if (pos == STRING_NPOS)
    return string_from_string(allocator, str);

  if (rlen > SIZE_MAX - (str->len - nlen))
    panic("string_replace: length overflow");
  size_t new_len = str->len - nlen + rlen;

  string_t *out = string_new(allocator);
  if (!out)
    return NULL;
  string_grow(out, new_len + 1);
  if (pos > 0)
    memcpy(out->data, str->data, pos);
  if (rlen > 0)
    memcpy(out->data + pos, replacement, rlen);
  memcpy(out->data + pos + rlen, str->data + pos + nlen,
         str->len - pos - nlen);
  out->len = new_len;
  out->data[new_len] = '\0';
  return out;
}

string_t *string_replace_all(allocator_t *allocator, const string_t *str,
                             const char *needle, const char *replacement) {
  if (!allocator || !str || !needle)
    return NULL;
  if (needle[0] == '\0')
    return string_from_string(allocator, str);

  size_t nlen = strlen(needle);
  size_t rlen = replacement ? strlen(replacement) : 0;

  /* Count matches first. */
  size_t count = 0;
  size_t i = 0;
  for (;;) {
    i = mem_search(str->data, str->len, needle, nlen, i);
    if (i == STRING_NPOS)
      break;
    count++;
    i += nlen;
  }
  if (count == 0)
    return string_from_string(allocator, str);

  /* new_len = str->len + count * (rlen - nlen); only grows when rlen > nlen. */
  if (rlen > nlen) {
    size_t delta = rlen - nlen;
    if (count > (SIZE_MAX - str->len) / delta)
      panic("string_replace_all: length overflow");
  }
  size_t new_len = str->len + count * (rlen - nlen);

  string_t *out = string_new(allocator);
  if (!out)
    return NULL;
  string_grow(out, new_len + 1);

  size_t src_pos = 0;
  size_t dst_pos = 0;
  for (;;) {
    i = mem_search(str->data, str->len, needle, nlen, src_pos);
    if (i == STRING_NPOS)
      break;
    size_t gap = i - src_pos;
    if (gap > 0)
      memcpy(out->data + dst_pos, str->data + src_pos, gap);
    dst_pos += gap;
    if (rlen > 0)
      memcpy(out->data + dst_pos, replacement, rlen);
    dst_pos += rlen;
    src_pos = i + nlen;
  }
  size_t tail = str->len - src_pos;
  if (tail > 0)
    memcpy(out->data + dst_pos, str->data + src_pos, tail);
  dst_pos += tail;

  out->len = dst_pos;
  out->data[dst_pos] = '\0';
  return out;
}

/* ---- Callbacks for string_class ---- */

static void string_dispose(void *self, allocator_t *allocator) {
  string_t *str = (string_t *)self;
  if (!str)
    return;
  if (str->data) {
    void *old = str->data;
    allocator_free(str->allocator, &old);
    str->data = NULL;
  }
  str->len = 0;
  str->cap = 0;
  str->allocator = NULL;
  (void)allocator;
}

static void string_move_cb(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  string_t *dst = (string_t *)self;
  string_t *src = (string_t *)another;
  if (!dst || !src)
    return;

  dst->data = src->data;
  dst->len = src->len;
  dst->cap = src->cap;
  dst->allocator = src->allocator;

  src->data = NULL;
  src->len = 0;
  src->cap = 0;
  src->allocator = NULL;
}

static void string_clone_cb(void *self, allocator_t *allocator, void *another) {
  string_t *dst = (string_t *)self;
  string_t *src = (string_t *)another;
  if (!dst || !src)
    return;

  dst->allocator = allocator;
  dst->len = 0;
  dst->cap = 0;
  dst->data = NULL;

  if (src->len > 0) {
    dst->data = (char *)allocator_new(allocator, &char_class, src->len + 1);
    memcpy(dst->data, src->data, src->len + 1);
    dst->len = src->len;
    dst->cap = src->len + 1;
  }
}
