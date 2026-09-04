#include "core/rbtree.h"
#include "core/panic.h"
#include <malloc.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Internal: node structure ---- */

typedef enum _rb_color_t { RB_RED, RB_BLACK } rb_color_t;

typedef struct _rb_node_t {
  void *element;
  rb_color_t color;
  struct _rb_node_t *left;
  struct _rb_node_t *right;
  struct _rb_node_t *parent;
} rb_node_t;

/* ---- Internal: rbtree_t definition ---- */

struct _rbtree_t {
  rb_node_t *root;
  size_t size;
  rbtree_cmp_fn_t cmp_fn;
  bool owns_element;
  allocator_t *allocator; /* stored for node alloc/free in remove */
};

/* ---- Internal: class for rb_node_t ---- */

static class_t node_class = {
    .name = "rb_node_t",
    .size = sizeof(rb_node_t),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};

/* ---- Internal: class for rbtree_t ---- */

static void rbtree_dispose(void *self, allocator_t *allocator);
static void rbtree_move_cb(void *self, allocator_t *allocator, void *another);
static void rbtree_clone_cb(void *self, allocator_t *allocator, void *another);

static class_t rbtree_class = {
    .name = "rbtree_t",
    .size = sizeof(rbtree_t),
    .move_fn = rbtree_move_cb,
    .clone_fn = rbtree_clone_cb,
    .dispose_fn = rbtree_dispose,
};

/* ---- Internal: node helpers ---- */

static rb_node_t *node_new(allocator_t *allocator, void *element) {
  rb_node_t *n = (rb_node_t *)allocator_new(allocator, &node_class, 1);
  n->element = element;
  n->color = RB_RED;
  n->left = NULL;
  n->right = NULL;
  n->parent = NULL;
  return n;
}

/* ---- Internal: rotate helpers ---- */

static void rotate_left(rbtree_t *tree, rb_node_t *x) {
  rb_node_t *y = x->right;
  x->right = y->left;
  if (y->left)
    y->left->parent = x;
  y->parent = x->parent;
  if (!x->parent)
    tree->root = y;
  else if (x == x->parent->left)
    x->parent->left = y;
  else
    x->parent->right = y;
  y->left = x;
  x->parent = y;
}

static void rotate_right(rbtree_t *tree, rb_node_t *x) {
  rb_node_t *y = x->left;
  x->left = y->right;
  if (y->right)
    y->right->parent = x;
  y->parent = x->parent;
  if (!x->parent)
    tree->root = y;
  else if (x == x->parent->right)
    x->parent->right = y;
  else
    x->parent->left = y;
  y->right = x;
  x->parent = y;
}

/* ---- Internal: insert fixup ---- */

static void insert_fixup(rbtree_t *tree, rb_node_t *z) {
  while (z->parent && z->parent->color == RB_RED) {
    if (z->parent == z->parent->parent->left) {
      rb_node_t *y = z->parent->parent->right;
      if (y && y->color == RB_RED) {
        z->parent->color = RB_BLACK;
        y->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) {
          z = z->parent;
          rotate_left(tree, z);
        }
        z->parent->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        rotate_right(tree, z->parent->parent);
      }
    } else {
      rb_node_t *y = z->parent->parent->left;
      if (y && y->color == RB_RED) {
        z->parent->color = RB_BLACK;
        y->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) {
          z = z->parent;
          rotate_right(tree, z);
        }
        z->parent->color = RB_BLACK;
        z->parent->parent->color = RB_RED;
        rotate_left(tree, z->parent->parent);
      }
    }
  }
  tree->root->color = RB_BLACK;
}

/* ---- Internal: transplant ---- */

static void transplant(rbtree_t *tree, rb_node_t *u, rb_node_t *v) {
  if (!u->parent)
    tree->root = v;
  else if (u == u->parent->left)
    u->parent->left = v;
  else
    u->parent->right = v;
  if (v)
    v->parent = u->parent;
}

/* ---- Internal: subtree minimum / maximum ---- */

static rb_node_t *subtree_min(rb_node_t *x) {
  while (x->left)
    x = x->left;
  return x;
}

static rb_node_t *subtree_max(rb_node_t *x) {
  while (x->right)
    x = x->right;
  return x;
}

/* ---- Internal: delete fixup ---- */

static void delete_fixup(rbtree_t *tree, rb_node_t *x, rb_node_t *x_parent) {
  while (x != tree->root && (!x || x->color == RB_BLACK)) {
    if (x == x_parent->left) {
      rb_node_t *w = x_parent->right;
      if (w && w->color == RB_RED) {
        w->color = RB_BLACK;
        x_parent->color = RB_RED;
        rotate_left(tree, x_parent);
        w = x_parent->right;
      }
      if ((!w->left || w->left->color == RB_BLACK) &&
          (!w->right || w->right->color == RB_BLACK)) {
        w->color = RB_RED;
        x = x_parent;
        x_parent = x->parent;
      } else {
        if (!w->right || w->right->color == RB_BLACK) {
          if (w->left)
            w->left->color = RB_BLACK;
          w->color = RB_RED;
          rotate_right(tree, w);
          w = x_parent->right;
        }
        w->color = x_parent->color;
        x_parent->color = RB_BLACK;
        if (w->right)
          w->right->color = RB_BLACK;
        rotate_left(tree, x_parent);
        x = tree->root;
        x_parent = NULL;
      }
    } else {
      rb_node_t *w = x_parent->left;
      if (w && w->color == RB_RED) {
        w->color = RB_BLACK;
        x_parent->color = RB_RED;
        rotate_right(tree, x_parent);
        w = x_parent->left;
      }
      if ((!w->right || w->right->color == RB_BLACK) &&
          (!w->left || w->left->color == RB_BLACK)) {
        w->color = RB_RED;
        x = x_parent;
        x_parent = x->parent;
      } else {
        if (!w->left || w->left->color == RB_BLACK) {
          if (w->right)
            w->right->color = RB_BLACK;
          w->color = RB_RED;
          rotate_left(tree, w);
          w = x_parent->left;
        }
        w->color = x_parent->color;
        x_parent->color = RB_BLACK;
        if (w->left)
          w->left->color = RB_BLACK;
        rotate_right(tree, x_parent);
        x = tree->root;
        x_parent = NULL;
      }
    }
  }
  if (x)
    x->color = RB_BLACK;
}

/* ---- Internal: recursive free ---- */

static void free_subtree(rbtree_t *tree, allocator_t *allocator,
                         rb_node_t *node) {
  if (!node)
    return;
  free_subtree(tree, allocator, node->left);
  free_subtree(tree, allocator, node->right);
  if (tree->owns_element && node->element) {
    void *elem = node->element;
    allocator_free(allocator, &elem);
  }
  allocator_free(allocator, (void **)&node);
}

/* ---- Internal: clone subtree ---- */

static rb_node_t *clone_subtree(const rbtree_t *src_tree,
                                allocator_t *allocator,
                                const rb_node_t *src_node,
                                rb_node_t *parent) {
  if (!src_node)
    return NULL;

  void *element;
  if (src_tree->owns_element && src_node->element) {
    void *elem_ptr = src_node->element;
    element = allocator_clone(allocator, &elem_ptr);
  } else {
    element = src_node->element;
  }

  rb_node_t *n = node_new(allocator, element);
  n->color = src_node->color;
  n->parent = parent;
  n->left = clone_subtree(src_tree, allocator, src_node->left, n);
  n->right = clone_subtree(src_tree, allocator, src_node->right, n);
  return n;
}

/* ---- Construction / destruction ---- */

rbtree_t *rbtree_new(allocator_t *allocator, rbtree_cmp_fn_t cmp_fn,
                     bool owns_element) {
  if (!allocator || !cmp_fn)
    return NULL;

  rbtree_t *tree = (rbtree_t *)allocator_new(allocator, &rbtree_class, 1);
  tree->root = NULL;
  tree->size = 0;
  tree->cmp_fn = cmp_fn;
  tree->owns_element = owns_element;
  tree->allocator = allocator;
  return tree;
}

void rbtree_free(allocator_t *allocator, rbtree_t **tree) {
  if (!allocator || !tree || !*tree)
    return;
  allocator_free(allocator, (void **)tree);
}

/* ---- Insertion ---- */

void *rbtree_insert(rbtree_t *tree, allocator_t *allocator, void *element) {
  if (!tree || !element)
    return NULL;

  rb_node_t *parent = NULL;
  rb_node_t *cur = tree->root;
  int cmp = 0;

  while (cur) {
    parent = cur;
    cmp = tree->cmp_fn(element, cur->element);
    if (cmp < 0)
      cur = cur->left;
    else if (cmp > 0)
      cur = cur->right;
    else {
      void *old = cur->element;
      cur->element = element;
      return old;
    }
  }

  rb_node_t *z = node_new(allocator, element);
  z->parent = parent;
  if (!parent)
    tree->root = z;
  else if (cmp < 0)
    parent->left = z;
  else
    parent->right = z;

  insert_fixup(tree, z);
  tree->size++;
  return NULL;
}

/* ---- Removal ---- */

void *rbtree_remove(rbtree_t *tree, const void *key) {
  if (!tree || !key)
    return NULL;

  rb_node_t *z = tree->root;
  while (z) {
    int cmp = tree->cmp_fn(key, z->element);
    if (cmp < 0)
      z = z->left;
    else if (cmp > 0)
      z = z->right;
    else
      break;
  }
  if (!z)
    return NULL;

  void *removed = z->element;
  rb_color_t y_original_color = z->color;
  rb_node_t *x;
  rb_node_t *x_parent;

  if (!z->left) {
    x = z->right;
    x_parent = z->parent;
    transplant(tree, z, z->right);
  } else if (!z->right) {
    x = z->left;
    x_parent = z->parent;
    transplant(tree, z, z->left);
  } else {
    rb_node_t *y = subtree_min(z->right);
    y_original_color = y->color;
    x = y->right;
    if (y->parent == z) {
      x_parent = y;
    } else {
      x_parent = y->parent;
      transplant(tree, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }
    transplant(tree, z, y);
    y->left = z->left;
    y->left->parent = y;
    y->color = z->color;
  }

  /* Free the node structure (not the element — caller gets ownership) */
  z->left = NULL;
  z->right = NULL;
  z->element = NULL;
  allocator_free(tree->allocator, (void **)&z);

  if (y_original_color == RB_BLACK)
    delete_fixup(tree, x, x_parent);

  tree->size--;
  return removed;
}

/* ---- Lookup ---- */

void *rbtree_find(const rbtree_t *tree, const void *key) {
  if (!tree || !key)
    return NULL;

  rb_node_t *cur = tree->root;
  while (cur) {
    int cmp = tree->cmp_fn(key, cur->element);
    if (cmp < 0)
      cur = cur->left;
    else if (cmp > 0)
      cur = cur->right;
    else
      return cur->element;
  }
  return NULL;
}

bool rbtree_contains(const rbtree_t *tree, const void *key) {
  return rbtree_find(tree, key) != NULL;
}

/* ---- Properties ---- */

size_t rbtree_size(const rbtree_t *tree) {
  if (!tree)
    return 0;
  return tree->size;
}

bool rbtree_is_empty(const rbtree_t *tree) {
  if (!tree)
    return true;
  return tree->size == 0;
}

void *rbtree_min(const rbtree_t *tree) {
  if (!tree || !tree->root)
    return NULL;
  return subtree_min(tree->root)->element;
}

void *rbtree_max(const rbtree_t *tree) {
  if (!tree || !tree->root)
    return NULL;
  return subtree_max(tree->root)->element;
}

/* ---- Ownership query ---- */

bool rbtree_owns_element(const rbtree_t *tree) {
  if (!tree)
    return false;
  return tree->owns_element;
}

/* ---- Traversal ---- */

static void foreach_node(rb_node_t *node, rbtree_visit_fn_t visit,
                         void *ctx) {
  if (!node)
    return;
  foreach_node(node->left, visit, ctx);
  visit(node->element, ctx);
  foreach_node(node->right, visit, ctx);
}

void rbtree_foreach(const rbtree_t *tree, rbtree_visit_fn_t visit,
                    void *ctx) {
  if (!tree || !visit)
    return;
  foreach_node(tree->root, visit, ctx);
}

/* ---- Callbacks for rbtree_class ---- */

static void rbtree_dispose(void *self, allocator_t *allocator) {
  rbtree_t *tree = (rbtree_t *)self;
  if (!tree)
    return;
  free_subtree(tree, allocator, tree->root);
  tree->root = NULL;
  tree->size = 0;
  tree->allocator = NULL;
}

static void rbtree_move_cb(void *self, allocator_t *allocator, void *another) {
  (void)allocator;
  rbtree_t *dst = (rbtree_t *)self;
  rbtree_t *src = (rbtree_t *)another;
  if (!dst || !src)
    return;

  dst->root = src->root;
  dst->size = src->size;
  dst->cmp_fn = src->cmp_fn;
  dst->owns_element = src->owns_element;
  dst->allocator = src->allocator;

  src->root = NULL;
  src->size = 0;
  src->allocator = NULL;
}

static void rbtree_clone_cb(void *self, allocator_t *allocator, void *another) {
  rbtree_t *dst = (rbtree_t *)self;
  rbtree_t *src = (rbtree_t *)another;
  if (!dst || !src)
    return;

  dst->cmp_fn = src->cmp_fn;
  dst->owns_element = src->owns_element;
  dst->allocator = allocator;
  dst->size = 0;
  dst->root = clone_subtree(src, allocator, src->root, NULL);

  /* Count nodes to set size */
  rb_node_t *n = dst->root;
  size_t count = 0;
  /* Simple recursive count not available; we traverse */
  /* Use the fact that clone_subtree preserves structure */
  if (src->root)
    dst->size = src->size;
}
