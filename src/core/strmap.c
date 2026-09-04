#include "core/strmap.h"
#include "core/panic.h"
#include "core/rbtree.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Internal: entry stored in the rbtree ---- */

typedef struct {
  char *key;    /* owned copy */
  void *value;
} strmap_entry_t;

/* ---- Internal: strmap_t definition ---- */

struct _strmap_t {
  rbtree_t *tree;          /* stores strmap_entry_t*, owns_element=false */
  vec_t *key_vec;          /* stores char* (same ptrs as entry->key), owns_element=false */
  bool owns_value;
  allocator_t *allocator;
};

/* ---- Internal: class for strmap_entry_t ---- */

static class_t entry_class = {
    .name = "strmap_entry",
    .size = sizeof(strmap_entry_t),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- Internal: class for strmap_t ---- */

static void strmap_dispose(void *self, allocator_t *allocator);
static void strmap_move_cb(void *self, allocator_t *allocator, void *another);
static void strmap_clone_cb(void *self, allocator_t *allocator, void *another);

static class_t strmap_class = {
    .name = "strmap_t",
    .size = sizeof(strmap_t),
    .move_fn = strmap_move_cb,
    .clone_fn = strmap_clone_cb,
    .dispose_fn = strmap_dispose,
};

/* ---- Internal: class for key strings (size=1, count=strlen+1) ---- */

static class_t str_class = {
    .name = "str",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- Internal: entry comparison (always strcmp on key) ---- */

static int entry_cmp(const void *a, const void *b) {
  const strmap_entry_t *ea = (const strmap_entry_t *)a;
  const strmap_entry_t *eb = (const strmap_entry_t *)b;
  return strcmp(ea->key, eb->key);
}

/* ---- Internal: duplicate a key string via allocator ---- */

static char *key_dup(allocator_t *allocator, const char *src) {
  size_t len = strlen(src) + 1;
  char *dst = (char *)allocator_new(allocator, &str_class, len);
  memcpy(dst, src, len);
  return dst;
}

/* ---- Internal: free a key string via allocator ---- */

static void key_free(allocator_t *allocator, char **key) {
  allocator_free(allocator, (void **)key);
}

/* ---- Internal: context for foreach callbacks ---- */

typedef struct {
  allocator_t *allocator;
  bool owns_value;
} dispose_ctx_t;

static void dispose_entry(void *element, void *ctx) {
  strmap_entry_t *entry = (strmap_entry_t *)element;
  dispose_ctx_t *dctx = (dispose_ctx_t *)ctx;

  if (entry->key) {
    key_free(dctx->allocator, &entry->key);
  }
  if (dctx->owns_value && entry->value) {
    void *v = entry->value;
    allocator_free(dctx->allocator, &v);
    entry->value = NULL;
  }
  allocator_free(dctx->allocator, (void **)&entry);
}

typedef struct {
  const strmap_t *src;
  strmap_t *dst;
  allocator_t *allocator;
} clone_ctx_t;

static void clone_entry(void *element, void *ctx) {
  strmap_entry_t *src_entry = (strmap_entry_t *)element;
  clone_ctx_t *cctx = (clone_ctx_t *)ctx;

  void *value = src_entry->value;
  if (cctx->src->owns_value && value) {
    void *v = value;
    value = allocator_clone(cctx->allocator, &v);
  }

  strmap_insert(cctx->dst, cctx->allocator, src_entry->key, value);
}

/* ---- Construction / destruction ---- */

strmap_t *strmap_new(allocator_t *allocator, bool owns_value) {
  if (!allocator)
    return NULL;

  strmap_t *map = (strmap_t *)allocator_new(allocator, &strmap_class, 1);
  map->owns_value = owns_value;
  map->allocator = allocator;

  map->tree = rbtree_new(allocator, entry_cmp, false);
  map->key_vec = vec_new(allocator, false);

  return map;
}

void strmap_free(allocator_t *allocator, strmap_t **map) {
  if (!allocator || !map || !*map)
    return;
  allocator_free(allocator, (void **)map);
}

/* ---- Insertion ---- */

void *strmap_insert(strmap_t *map, allocator_t *allocator, const char *key,
                    void *value) {
  if (!map || !key)
    return NULL;

  /* Duplicate the key for the new entry */
  char *key_copy = key_dup(allocator, key);

  /* Create an entry for insertion/search */
  strmap_entry_t *entry =
      (strmap_entry_t *)allocator_new(allocator, &entry_class, 1);
  entry->key = key_copy;
  entry->value = value;

  void *old_element = rbtree_insert(map->tree, allocator, entry);

  if (old_element) {
    /* Key already existed: replace value, keep old key, free new key copy */
    strmap_entry_t *old_entry = (strmap_entry_t *)old_element;
    void *old_value = old_entry->value;

    /* Put old key into the new entry (now in tree) */
    entry->key = old_entry->key;
    /* entry->value stays as the new value */

    /* Free the old entry struct (keeping its key which we moved) */
    old_entry->key = NULL;
    old_entry->value = NULL;
    allocator_free(allocator, (void **)&old_entry);

    /* Free the new key copy (we're using the old one) */
    key_free(allocator, &key_copy);

    return old_value;
  }

  /* New key: push the copy to key_vec */
  vec_push(map->key_vec, allocator, key_copy);
  return NULL;
}

/* ---- Removal ---- */

static void remove_key_from_vec(strmap_t *map, const char *key) {
  size_t len = vec_len(map->key_vec);
  for (size_t i = 0; i < len; i++) {
    if (vec_get(map->key_vec, i) == (const void *)key) {
      vec_remove(map->key_vec, i);
      return;
    }
  }
}

void *strmap_remove(strmap_t *map, const char *key) {
  if (!map || !key)
    return NULL;

  /* Create a temporary search entry */
  strmap_entry_t search;
  search.key = (char *)key;
  search.value = NULL;

  /* Find the actual entry first to get the stored key pointer */
  strmap_entry_t *found = (strmap_entry_t *)rbtree_find(map->tree, &search);
  if (!found)
    return NULL;

  char *actual_key = found->key;
  void *old_value = found->value;

  /* Remove from rbtree */
  strmap_entry_t *removed =
      (strmap_entry_t *)rbtree_remove(map->tree, &search);
  if (!removed)
    return NULL;

  /* Remove from key_vec by pointer identity */
  remove_key_from_vec(map, actual_key);

  /* Free the key copy */
  key_free(map->allocator, &removed->key);

  /* Free the entry struct */
  removed->value = NULL;
  allocator_free(map->allocator, (void **)&removed);

  return old_value;
}

/* ---- Lookup ---- */

void *strmap_get(const strmap_t *map, const char *key) {
  if (!map || !key)
    return NULL;

  strmap_entry_t search;
  search.key = (char *)key;
  search.value = NULL;

  strmap_entry_t *found = (strmap_entry_t *)rbtree_find(map->tree, &search);
  return found ? found->value : NULL;
}

bool strmap_contains(const strmap_t *map, const char *key) {
  if (!map || !key)
    return false;

  strmap_entry_t search;
  search.key = (char *)key;
  search.value = NULL;

  return rbtree_find(map->tree, &search) != NULL;
}

/* ---- Properties ---- */

size_t strmap_size(const strmap_t *map) {
  if (!map)
    return 0;
  return rbtree_size(map->tree);
}

bool strmap_is_empty(const strmap_t *map) {
  if (!map)
    return true;
  return rbtree_is_empty(map->tree);
}

const vec_t *strmap_keys(const strmap_t *map) {
  if (!map)
    return NULL;
  return map->key_vec;
}

/* ---- Ownership query ---- */

bool strmap_owns_value(const strmap_t *map) {
  if (!map)
    return false;
  return map->owns_value;
}

/* ---- Callbacks for strmap_class ---- */

static void strmap_dispose(void *self, allocator_t *allocator) {
  strmap_t *map = (strmap_t *)self;
  if (!map)
    return;

  /* Free all entries (and their keys/values) via rbtree_foreach */
  dispose_ctx_t dctx = {
      .allocator = allocator,
      .owns_value = map->owns_value,
  };
  rbtree_foreach(map->tree, dispose_entry, &dctx);

  /* Free the tree and key_vec (no elements left) */
  rbtree_free(allocator, &map->tree);
  vec_free(allocator, &map->key_vec);

  map->allocator = NULL;
}

static void strmap_move_cb(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  strmap_t *dst = (strmap_t *)self;
  strmap_t *src = (strmap_t *)another;
  if (!dst || !src)
    return;

  dst->tree = src->tree;
  dst->key_vec = src->key_vec;
  dst->owns_value = src->owns_value;
  dst->allocator = src->allocator;

  src->tree = NULL;
  src->key_vec = NULL;
  src->allocator = NULL;
}

static void strmap_clone_cb(void *self, allocator_t *allocator, void *another) {
  strmap_t *dst = (strmap_t *)self;
  strmap_t *src = (strmap_t *)another;
  if (!dst || !src)
    return;

  dst->owns_value = src->owns_value;
  dst->allocator = allocator;

  dst->tree = rbtree_new(allocator, entry_cmp, false);
  dst->key_vec = vec_new(allocator, false);

  clone_ctx_t cctx = {
      .src = src,
      .dst = dst,
      .allocator = allocator,
  };
  rbtree_foreach(src->tree, clone_entry, &cctx);
}
