# clux core: RbTree & Red-Black Tree

> 本文档是 `src/core/rbtree.c` 的代码级约束文档，不是架构设计文档。
> 后续开发者在修改 rbtree 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 rbtree_t

不透明类型，基于红黑树实现的有序集合，持有比较函数、所有权标志和 allocator 引用。

```
struct _rbtree_t {                 // 定义在 rbtree.c 内部，不暴露
    rb_node_t       *root;         // 根节点
    size_t           size;         // 元素数量
    rbtree_cmp_fn_t  cmp_fn;       // 比较函数（不可为 NULL）
    bool             owns_element; // true: dispose 释放元素, clone 深拷贝元素
    allocator_t     *allocator;    // 存储 allocator 用于 node 分配/释放
};
```

**规则：**
- `rbtree_t` 的定义不暴露在头文件中，外部只能通过指针操作。
- `cmp_fn` 不可为 NULL，创建时确定，之后不可更改。
- `owns_element` 在创建时确定，之后不可更改。
- `allocator` 存储在 `rbtree_t` 中，`rbtree_remove` 等不需要额外传入 allocator。

### 1.2 owns_element 语义

| `owns_element` | `rbtree_free` 行为 | `allocator_clone` 行为 |
|----------------|-------------------|----------------------|
| `true` | 递归遍历所有节点，对每个非 NULL 元素调用 `allocator_free` | 遍历所有节点，对每个元素调用 `allocator_clone`（深拷贝） |
| `false` | 不释放元素，仅释放节点结构 | 直接复制元素指针（浅拷贝） |

### 1.3 rb_node_t

```
struct _rb_node_t {
    void          *element;  // 用户数据指针
    rb_color_t     color;    // RB_RED 或 RB_BLACK
    rb_node_t     *left;
    rb_node_t     *right;
    rb_node_t     *parent;
};
```

每个 `rb_node_t` 通过 `allocator_new(allocator, &node_class, 1)` 分配。

---

## 2. 内存布局

### 2.1 两级分配关系

```
allocator_new(rbtree_class, 1) ──→ [header | rbtree_t]
                                           │
                                           ├── rbtree_t.root ──→ rb_node_t ──→ 用户对象
                                           │                       ├── left ──→ ...
                                           │                       └── right ──→ ...
                                           └── rbtree_t.allocator ──→ allocator_t
```

每个 `rb_node_t` 自身也通过 `allocator_new(node_class, 1)` 分配。

---

## 3. 比较函数契约

```c
typedef int (*rbtree_cmp_fn_t)(const void *a, const void *b);
```

- 返回 `< 0` 表示 `a < b`
- 返回 `= 0` 表示 `a == b`
- 返回 `> 0` 表示 `a > b`
- 必须满足严格弱序（irreflexive, asymmetric, transitive）
- **禁止** 比较结果与元素地址相关（如直接比较指针值），否则红黑树的不变量会失效

---

## 4. 操作语义

### 4.1 insert

```
void *rbtree_insert(rbtree_t *tree, allocator_t *allocator, void *element);
```

- 在树中找到插入位置
- 若存在相等元素（`cmp_fn` 返回 0）：替换旧元素，返回旧指针
- 若不存在：创建新节点，插入后修复红黑性质
- 返回 `NULL` 表示新插入，非 `NULL` 表示替换的旧元素

**约束：** 替换时旧元素不被释放（即使 `owns_element=true`），所有权转移给调用方。

### 4.2 remove

```
void *rbtree_remove(rbtree_t *tree, const void *key);
```

- 查找与 `key` 相等的元素
- 若找到：移除节点，修复红黑性质，返回被移除的元素
- 若未找到：返回 `NULL`

**约束：** 返回的元素不被释放（即使 `owns_element=true`），所有权转移给调用方。

### 4.3 find / contains

```
void *rbtree_find(const rbtree_t *tree, const void *key);
bool rbtree_contains(const rbtree_t *tree, const void *key);
```

- O(log n) 查找

### 4.4 min / max

```
void *rbtree_min(const rbtree_t *tree);
void *rbtree_max(const rbtree_t *tree);
```

- 返回最小/最大元素，O(log n)
- 空树返回 `NULL`

---

## 5. 回调契约

### 5.1 rbtree_class 回调

**rbtree_dispose(self, allocator)：**
1. 递归释放所有节点（后序遍历：left → right → self）
2. 若 `owns_element=true`：对每个节点的 `element` 调用 `allocator_free`
3. 释放 `rb_node_t` 结构体
4. 置 `root=NULL`, `size=0`, `allocator=NULL`

**rbtree_move_cb(self, allocator, another)：**
1. 转移 `root`、`size`、`cmp_fn`、`owns_element`、`allocator` 从 `another` 到 `self`
2. 置 `another->root=NULL`, `another->size=0`, `another->allocator=NULL`

**约束：** move 后源的 `dispose_fn` 被 `allocator_free` 调用，此时 `root=NULL`，安全跳过。

**rbtree_clone_cb(self, allocator, another)：**
1. 转移 `cmp_fn`、`owns_element`
2. 递归克隆节点树：
   - `owns_element=true`：对每个元素调用 `allocator_clone`
   - `owns_element=false`：直接复制元素指针
3. 不使用 `memcpy` 复制指针——所有元素通过 API 语义操作

---

## 6. 红黑树不变量

实现遵循标准红黑树性质：

1. 每个节点是红色或黑色
2. 根节点是黑色
3. 叶节点（NULL）是黑色
4. 红色节点的子节点必须是黑色
5. 从任一节点到其所有后代叶节点的路径包含相同数量的黑色节点

**插入修复**：`insert_fixup` 处理 3 种 case（及其对称 case）。
**删除修复**：`delete_fixup` 处理 4 种 case（及其对称 case）。

---

## 7. 线程安全

- `rbtree_t` 的只读操作（find、contains、size、min、max）可安全多线程读取。
- 所有修改操作（insert、remove）非线程安全。
- 用户需自行保证多线程访问时的同步。

---

## 8. 禁止事项

1. **禁止** 直接操作 `rbtree_t` 的内部字段——它是不透明类型。
2. **禁止** 修改 `cmp_fn` 返回的结果不一致——必须满足严格弱序。
3. **禁止** 对 `rbtree_insert` 返回的旧元素和 `rbtree_remove` 返回的元素遗忘——调用方获得所有权。
4. **禁止** 在 `owns_element=false` 的树上假设元素会被释放。
5. **禁止** 将不同 allocator 分配的元素混入 `owns_element=true` 的树——释放时 alloc/free 对必须匹配。
6. **禁止** 对 NULL 值调用 `rbtree_insert`——会被静默忽略。
