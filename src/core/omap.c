#include "core/omap.h"
#include "core/panic.h"
#include "core/rbtree.h"
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Internal: entry stored in the rbtree ---- */

typedef struct {
  void *key;
  void *value;
} omap_entry_t;

/* ---- Internal: omap_t definition ---- */

struct _omap_t {
  rbtree_t *tree;          /* stores omap_entry_t*, owns_element=false */
  vec_t *key_vec;          /* stores key*, owns_element=false, preserves order */
  omap_cmp_fn_t cmp_fn;
  bool owns_key;
  bool owns_value;
  allocator_t *allocator;
};

/* ---- Internal: class for omap_entry_t ---- */

static class_t entry_class = {
    .name = "omap_entry",
    .size = sizeof(omap_entry_t),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- Internal: class for omap_t ---- */

static void omap_dispose(void *self, allocator_t *allocator);
static void omap_move_cb(void *self, allocator_t *allocator, void *another);
static void omap_clone_cb(void *self, allocator_t *allocator, void *another);

static class_t omap_class = {
    .name = "omap_t",
    .size = sizeof(omap_t),
    .move_fn = omap_move_cb,
    .clone_fn = omap_clone_cb,
    .dispose_fn = omap_dispose,
};

/* ---- Thread-local context for entry_cmp adapter ---- */

#ifdef _MSC_VER
#define TLS __declspec(thread)
#else
#define TLS __thread
#endif

static TLS omap_cmp_fn_t g_omap_cmp = NULL;

static int entry_cmp(const void *a, const void *b) {
  const omap_entry_t *ea = (const omap_entry_t *)a;
  const omap_entry_t *eb = (const omap_entry_t *)b;
  return g_omap_cmp(ea->key, eb->key);
}

/* ---- Internal: context for foreach callbacks ---- */

typedef struct {
  allocator_t *allocator;
  bool owns_key;
  bool owns_value;
} dispose_ctx_t;

static void dispose_entry(void *element, void *ctx) {
  omap_entry_t *entry = (omap_entry_t *)element;
  dispose_ctx_t *dctx = (dispose_ctx_t *)ctx;

  if (dctx->owns_key && entry->key) {
    void *k = entry->key;
    allocator_free(dctx->allocator, &k);
    entry->key = NULL;
  }
  if (dctx->owns_value && entry->value) {
    void *v = entry->value;
    allocator_free(dctx->allocator, &v);
    entry->value = NULL;
  }
  allocator_free(dctx->allocator, (void **)&entry);
}

typedef struct {
  const omap_t *src;
  omap_t *dst;
  allocator_t *allocator;
} clone_ctx_t;

static void clone_entry(void *element, void *ctx) {
  omap_entry_t *src_entry = (omap_entry_t *)element;
  clone_ctx_t *cctx = (clone_ctx_t *)ctx;

  void *key = src_entry->key;
  void *value = src_entry->value;

  /* Clone or copy key/value based on ownership */
  if (cctx->src->owns_key && key) {
    void *k = key;
    key = allocator_clone(cctx->allocator, &k);
  }
  if (cctx->src->owns_value && value) {
    void *v = value;
    value = allocator_clone(cctx->allocator, &v);
  }

  /* Insert into destination (sets g_omap_cmp internally) */
  g_omap_cmp = cctx->dst->cmp_fn;
  omap_insert(cctx->dst, cctx->allocator, key, value);
}

/* ---- Construction / destruction ---- */

omap_t *omap_new(allocator_t *allocator, omap_cmp_fn_t cmp_fn,
                 bool owns_key, bool owns_value) {
  if (!allocator || !cmp_fn)
    return NULL;

  omap_t *map = (omap_t *)allocator_new(allocator, &omap_class, 1);
  map->cmp_fn = cmp_fn;
  map->owns_key = owns_key;
  map->owns_value = owns_value;
  map->allocator = allocator;

  g_omap_cmp = cmp_fn;
  map->tree = rbtree_new(allocator, entry_cmp, false);
  map->key_vec = vec_new(allocator, false);

  return map;
}

void omap_free(allocator_t *allocator, omap_t **map) {
  if (!allocator || !map || !*map)
    return;
  allocator_free(allocator, (void **)map);
}

/* ---- Insertion ---- */

void *omap_insert(omap_t *map, allocator_t *allocator, void *key,
                  void *value) {
  if (!map || !key)
    return NULL;

  /* Create an entry for insertion/search */
  omap_entry_t *entry =
      (omap_entry_t *)allocator_new(allocator, &entry_class, 1);
  entry->key = key;
  entry->value = value;

  g_omap_cmp = map->cmp_fn;
  void *old_element = rbtree_insert(map->tree, allocator, entry);

  if (old_element) {
    /* Key already existed: replace value, keep old key */
    omap_entry_t *old_entry = (omap_entry_t *)old_element;
    void *old_value = old_entry->value;

    /* Update value in the existing entry (which is now in the tree) */
    /* The tree replaced the element, so the new entry is in the tree */
    /* and old_entry was returned. But rbtree_insert replaces the element
       pointer in the node, so the NEW entry is in the tree. */
    /* We need to: free the new entry (not needed), update old_entry's value */
    /* Actually: rbtree_insert puts `entry` into the tree and returns
       the old element. So now the tree has `entry` (with new key/value).
       We need to swap back to the old key and update value. */

    /* Put old key into the new entry (now in tree) */
    entry->key = old_entry->key;
    /* entry->value stays as the new value */

    /* Free the old entry struct (keeping its key which we moved) */
    old_entry->key = NULL;
    old_entry->value = NULL;
    allocator_free(allocator, (void **)&old_entry);

    /* If owns_key, free the new key (we're using the old one) */
    if (map->owns_key) {
      void *k = key;
      allocator_free(allocator, &k);
    }

    return old_value;
  }

  /* New key: push to key_vec */
  vec_push(map->key_vec, allocator, key);
  return NULL;
}

/* ---- Removal ---- */

static void remove_key_from_vec(omap_t *map, const void *key) {
  size_t len = vec_len(map->key_vec);
  for (size_t i = 0; i < len; i++) {
    if (vec_get(map->key_vec, i) == key) {
      vec_remove(map->key_vec, i);
      return;
    }
  }
}

void *omap_remove(omap_t *map, const void *key) {
  if (!map || !key)
    return NULL;

  /* Create a temporary search entry */
  omap_entry_t search;
  search.key = (void *)key;
  search.value = NULL;

  g_omap_cmp = map->cmp_fn;

  /* We need to use rbtree_remove, but it compares with cmp_fn which
     expects entries. We need a version that can find by key. */
  /* Since entry_cmp compares by key, and our search has the same key,
     rbtree_find will find the matching entry. Then we can use
     rbtree_remove with the entry's key (which is the actual key ptr). */

  void *found = rbtree_find(map->tree, &search);
  if (!found)
    return NULL;

  omap_entry_t *entry = (omap_entry_t *)found;
  void *actual_key = entry->key;
  void *old_value = entry->value;

  /* Remove from rbtree using the actual key stored in the entry */
  /* rbtree_remove compares using cmp_fn with the raw element,
     but entry_cmp expects entries. We need to pass the entry itself
     as the key. Actually, rbtree_remove calls cmp_fn(key, element)
     where key is our search term and element is the stored entry.
     So we pass &search again. */
  g_omap_cmp = map->cmp_fn;
  void *removed_entry = rbtree_remove(map->tree, &search);
  if (!removed_entry)
    return NULL;

  entry = (omap_entry_t *)removed_entry;

  /* Remove from key_vec */
  remove_key_from_vec(map, actual_key);

  /* Free key if owned */
  if (map->owns_key && entry->key) {
    void *k = entry->key;
    allocator_free(map->allocator, &k);
    entry->key = NULL;
  }

  /* Free the entry struct */
  entry->value = NULL;
  allocator_free(map->allocator, (void **)&entry);

  return old_value;
}

/* ---- Lookup ---- */

void *omap_get(const omap_t *map, const void *key) {
  if (!map || !key)
    return NULL;

  omap_entry_t search;
  search.key = (void *)key;
  search.value = NULL;

  g_omap_cmp = map->cmp_fn;
  omap_entry_t *found = (omap_entry_t *)rbtree_find(map->tree, &search);
  return found ? found->value : NULL;
}

bool omap_contains(const omap_t *map, const void *key) {
  if (!map || !key)
    return false;

  omap_entry_t search;
  search.key = (void *)key;
  search.value = NULL;

  g_omap_cmp = map->cmp_fn;
  return rbtree_find(map->tree, &search) != NULL;
}

/* ---- Properties ---- */

size_t omap_size(const omap_t *map) {
  if (!map)
    return 0;
  return rbtree_size(map->tree);
}

bool omap_is_empty(const omap_t *map) {
  if (!map)
    return true;
  return rbtree_is_empty(map->tree);
}

const vec_t *omap_keys(const omap_t *map) {
  if (!map)
    return NULL;
  return map->key_vec;
}

/* ---- Ownership query ---- */

bool omap_owns_key(const omap_t *map) {
  if (!map)
    return false;
  return map->owns_key;
}

bool omap_owns_value(const omap_t *map) {
  if (!map)
    return false;
  return map->owns_value;
}

/* ---- Callbacks for omap_class ---- */

static void omap_dispose(void *self, allocator_t *allocator) {
  omap_t *map = (omap_t *)self;
  if (!map)
    return;

  /* Free all entries (and their key/value) via rbtree_foreach */
  dispose_ctx_t dctx = {
      .allocator = allocator,
      .owns_key = map->owns_key,
      .owns_value = map->owns_value,
  };
  rbtree_foreach(map->tree, dispose_entry, &dctx);

  /* Now the tree has no elements, free it */
  rbtree_free(allocator, &map->tree);
  vec_free(allocator, &map->key_vec);

  map->cmp_fn = NULL;
  map->allocator = NULL;
}

static void omap_move_cb(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  omap_t *dst = (omap_t *)self;
  omap_t *src = (omap_t *)another;
  if (!dst || !src)
    return;

  dst->tree = src->tree;
  dst->key_vec = src->key_vec;
  dst->cmp_fn = src->cmp_fn;
  dst->owns_key = src->owns_key;
  dst->owns_value = src->owns_value;
  dst->allocator = src->allocator;

  src->tree = NULL;
  src->key_vec = NULL;
  src->allocator = NULL;
}

static void omap_clone_cb(void *self, allocator_t *allocator, void *another) {
  omap_t *dst = (omap_t *)self;
  omap_t *src = (omap_t *)another;
  if (!dst || !src)
    return;

  dst->cmp_fn = src->cmp_fn;
  dst->owns_key = src->owns_key;
  dst->owns_value = src->owns_value;
  dst->allocator = allocator;

  g_omap_cmp = src->cmp_fn;
  dst->tree = rbtree_new(allocator, entry_cmp, false);
  dst->key_vec = vec_new(allocator, false);

  clone_ctx_t cctx = {
      .src = src,
      .dst = dst,
      .allocator = allocator,
  };
  rbtree_foreach(src->tree, clone_entry, &cctx);
}
