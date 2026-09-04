# clux core: OMap (Ordered Map)

> 本文档是 `src/core/omap.c` 的代码级约束文档。
> 后续开发者在修改 omap 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 omap_t

不透明类型，基于 rbtree + vec 实现的有序映射。rbtree 提供 O(log n) 查找，vec 保留插入顺序。

```
struct _omap_t {                     // 定义在 omap.c 内部，不暴露
    rbtree_t       *tree;            // 存储 omap_entry_t*, owns_element=false
    vec_t          *key_vec;         // 存储 key*, owns_element=false, 保留插入顺序
    omap_cmp_fn_t   cmp_fn;         // key 比较函数
    bool            owns_key;       // true: dispose 释放 key, clone 深拷贝 key
    bool            owns_value;     // true: dispose 释放 value, clone 深拷贝 value
    allocator_t    *allocator;      // 存储 allocator 用于内部操作
};
```

### 1.2 omap_entry_t（内部类型）

```
typedef struct {
    void *key;
    void *value;
} omap_entry_t;
```

rbtree 存储的是 `omap_entry_t*`，比较函数通过 `entry_cmp` adapter 从 entry 中提取 key 进行比较。

### 1.3 owns_key / owns_value 语义

| `owns_key` | `owns_value` | `omap_free` 行为 | `allocator_clone` 行为 |
|:---:|:---:|---|---|
| F | F | 不释放 key/value | 直接复制指针（浅拷贝） |
| T | F | 释放 key | clone key，直接复制 value |
| F | T | 释放 value | 直接复制 key，clone value |
| T | T | 释放 key 和 value | clone key 和 value（深拷贝） |

**约束：** `owns_key=true` 时所有 key 必须是由同一 allocator 分配的对象。

---

## 2. 双重索引结构

```
omap_t
 ├── rbtree_t ──→ [omap_entry_t{key,value}] (按 cmp_fn 排序，O(log n) 查找)
 │                    │
 │                    └── entry_cmp adapter ──→ cmp_fn(entry->key, entry->key)
 │
 └── vec_t ──→ [key*, key*, ...]  (按插入顺序，O(1) 追加，O(n) 删除)
```

### 2.1 entry_cmp adapter

由于 C 语言无法捕获闭包，`entry_cmp` 通过 TLS 变量 `g_omap_cmp` 获取当前 omap 的 `cmp_fn`。

**约束：** 所有 rbtree 操作前必须设置 `g_omap_cmp`。本项目单线程，TLS 仅用于传递比较函数上下文。

### 2.2 key_vec 中的指针

`key_vec` 存储的是与 entry 中 `key` 相同的指针。当删除 entry 时，通过指针值在 vec 中线性搜索并移除。

**复杂度：** omap_remove 的 vec 操作为 O(n)。若需更高效的删除，可改用 swap_remove（但不保序）或引入索引映射。

---

## 3. 操作语义

### 3.1 insert

```
void *omap_insert(omap_t *map, allocator_t *allocator, void *key, void *value);
```

1. 创建 `omap_entry_t{key, value}`
2. 插入 rbtree
3. 若 key 已存在：
   - 保留旧 key，替换 value
   - 若 `owns_key=true`：释放新 key（未使用）
   - 返回旧 value
4. 若 key 不存在：
   - 将 key 推入 `key_vec`
   - 返回 NULL

### 3.2 remove

```
void *omap_remove(omap_t *map, const void *key);
```

1. 在 rbtree 中查找 entry
2. 从 rbtree 移除 entry
3. 从 key_vec 中移除 key（线性搜索 + vec_remove，保序）
4. 若 `owns_key=true`：释放 key
5. 返回 value（**不释放**，所有权转移给调用方）

### 3.3 get / contains

```
void *omap_get(const omap_t *map, const void *key);
bool omap_contains(const omap_t *map, const void *key);
```

使用临时 `omap_entry_t` 作为搜索 key，通过 `entry_cmp` adapter 比对。

### 3.4 keys

```
const vec_t *omap_keys(const omap_t *map);
```

返回 key_vec 的只读视图。**约束：** 返回指针在 map 发生变异后可能失效（vec 扩容导致指针重分配）。

---

## 4. 回调契约

### 4.1 omap_class 回调

**omap_dispose(self, allocator)：**
1. `rbtree_foreach` 遍历所有 entry
2. 对每个 entry：根据 `owns_key/owns_value` 释放 key/value，释放 entry
3. `rbtree_free`（owns_element=false，entry 已在步骤 2 释放）
4. `vec_free`（owns_element=false，key 已在步骤 2 释放）
5. 置空所有字段

**omap_move_cb(self, allocator, another)：**
1. 转移 tree、key_vec、cmp_fn、owns_key、owns_value、allocator
2. 置源字段为 NULL

**约束：** move 后源的 dispose 安全（tree=NULL，key_vec=NULL）。

**omap_clone_cb(self, allocator, another)：**
1. 创建新 rbtree 和 vec
2. `rbtree_foreach` 遍历源 tree
3. 对每个 entry：根据 `owns_key/owns_value` clone 或复制 key/value
4. 通过 `omap_insert` 插入到新 map

---

## 5. 禁止事项

1. **禁止** 直接操作 `omap_t` 的内部字段——它是不透明类型。
2. **禁止** 修改 `cmp_fn` 返回不一致结果——必须满足严格弱序。
3. **禁止** 在 `owns_key=true` 时插入非 allocator 分配的 key。
4. **禁止** 在 `owns_value=true` 时插入非 allocator 分配的 value。
5. **禁止** 对 `omap_remove` 返回的 value 遗忘——调用方获得所有权。
6. **禁止** 缓存 `omap_keys()` 返回的指针跨过变异操作——vec 扩容会使其失效。
