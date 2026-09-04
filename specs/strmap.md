# clux core: StrMap (String-Keyed Ordered Map)

> 本文档是 `src/core/strmap.c` 的代码级约束文档。
> 后续开发者在修改 strmap 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 strmap_t

不透明类型，基于 rbtree + vec 实现的字符串键有序映射。rbtree 提供 O(log n) 查找，vec 保留插入顺序。

```
struct _strmap_t {                     // 定义在 strmap.c 内部，不暴露
    rbtree_t       *tree;             // 存储 strmap_entry_t*, owns_element=false
    vec_t          *key_vec;          // 存储 char*, owns_element=false, 保留插入顺序
    bool            owns_value;       // true: dispose 释放 value, clone 深拷贝 value
    allocator_t    *allocator;        // 存储 allocator 用于内部操作
};
```

### 1.2 strmap_entry_t（内部类型）

```
typedef struct {
    char *key;    // owned copy, 通过 key_dup 分配
    void *value;
} strmap_entry_t;
```

rbtree 存储的是 `strmap_entry_t*`，比较函数通过 `entry_cmp` 对 `entry->key` 调用 `strcmp`。

### 1.3 key 所有权

**strmap 始终拥有 key 的所有权。** 所有 key 字符串在 insert 时通过 `key_dup` 复制，在 remove/dispose 时通过 `key_free` 释放。调用方传入的 `const char *key` 仅在 insert 调用期间有效，之后 strmap 持有独立副本。

### 1.4 owns_value 语义

| `owns_value` | `strmap_free` 行为 | `allocator_clone` 行为 |
|:---:|---|---|
| F | 不释放 value | 直接复制指针（浅拷贝） |
| T | 释放 value | clone value（深拷贝） |

**约束：** `owns_value=true` 时所有 value 必须是由同一 allocator 分配的对象。

---

## 2. 双重索引结构

```
strmap_t
 ├── rbtree_t ──→ [strmap_entry_t{key,value}] (按 strcmp 排序，O(log n) 查找)
 │                    │
 │                    └── entry_cmp ──→ strcmp(entry->key, entry->key)
 │
 └── vec_t ──→ [char*, char*, ...]  (按插入顺序，O(1) 追加，O(n) 删除)
```

### 2.1 entry_cmp

strmap 的比较函数固定为 `strcmp`，无需 TLS 变量传递闭包上下文（与 omap 不同）。查找时构造临时 `strmap_entry_t` 作为搜索键。

### 2.2 key_vec 中的指针

`key_vec` 存储的是与 entry 中 `key` 相同的指针。当删除 entry 时，通过指针值在 vec 中线性搜索并移除。

**复杂度：** strmap_remove 的 vec 操作为 O(n)。若需更高效的删除，可改用 swap_remove（但不保序）或引入索引映射。

---

## 3. key 内存管理

### 3.1 key_dup / key_free

```
static char *key_dup(allocator_t *allocator, const char *src);
static void  key_free(allocator_t *allocator, char **key);
```

key 字符串通过 `allocator_new(allocator, &str_class, strlen(src)+1)` 分配。`str_class` 为内部静态 class_t（`size=1, name="str"`），配合 count 参数实现变长分配。

`key_free` 调用 `allocator_free`，通过双指针模式置 NULL。

### 3.2 重复 key 处理

insert 时若 key 已存在：
1. 保留旧 key 副本，替换 value
2. 释放新 key 副本（未使用）
3. 返回旧 value

---

## 4. 操作语义

### 4.1 insert

```
void *strmap_insert(strmap_t *map, allocator_t *allocator, const char *key, void *value);
```

1. `key_dup` 复制 key
2. 创建 `strmap_entry_t{key_copy, value}`
3. 插入 rbtree
4. 若 key 已存在：
   - 保留旧 key，替换 value
   - 释放新 key 副本
   - 返回旧 value
5. 若 key 不存在：
   - 将 key 副本推入 `key_vec`
   - 返回 NULL

### 4.2 remove

```
void *strmap_remove(strmap_t *map, const char *key);
```

1. 在 rbtree 中查找 entry
2. 从 rbtree 移除 entry
3. 从 key_vec 中移除 key 副本（线性搜索 + vec_remove，保序）
4. 释放 key 副本
5. 返回 value（**不释放**，所有权转移给调用方）

### 4.3 get / contains

```
void *strmap_get(const strmap_t *map, const char *key);
bool strmap_contains(const strmap_t *map, const char *key);
```

使用临时 `strmap_entry_t` 作为搜索键，通过 `entry_cmp`（即 `strcmp`）比对。

### 4.4 keys

```
const vec_t *strmap_keys(const strmap_t *map);
```

返回 key_vec 的只读视图。**约束：** 返回指针在 map 发生变异后可能失效（vec 扩容导致指针重分配）。

---

## 5. 回调契约

### 5.1 strmap_class 回调

**strmap_dispose(self, allocator)：**
1. `rbtree_foreach` 遍历所有 entry
2. 对每个 entry：`key_free` 释放 key，若 `owns_value` 则 `allocator_free` 释放 value，`allocator_free` 释放 entry
3. `rbtree_free`（owns_element=false，entry 已在步骤 2 释放）
4. `vec_free`（owns_element=false，key 已在步骤 2 释放）
5. 置空 allocator

**strmap_move_cb(self, allocator, another)：**
1. 转移 tree、key_vec、owns_value、allocator
2. 置源字段为 NULL

**约束：** move 后源的 dispose 安全（tree=NULL，key_vec=NULL）。

**strmap_clone_cb(self, allocator, another)：**
1. 创建新 rbtree 和 vec
2. `rbtree_foreach` 遍历源 tree
3. 对每个 entry：`strmap_insert` 插入到新 map（key 自动复制，value 根据 owns_value 决定 clone 或复制）

---

## 6. 与 omap 的区别

| 特性 | omap | strmap |
|------|------|--------|
| key 类型 | `void*`，任意类型 | `const char*`，固定字符串 |
| key 所有权 | 可配置 `owns_key` | 始终拥有（自动复制/释放） |
| 比较函数 | 调用方提供 `cmp_fn` | 固定 `strcmp` |
| TLS 依赖 | 需要 `g_omap_cmp` 传递闭包 | 不需要 |
| key 内存 | `owns_key=true` 时走 `allocator_free` | 通过 `key_dup/key_free`（基于 `str_class`） |

---

## 7. 禁止事项

1. **禁止** 直接操作 `strmap_t` 的内部字段——它是不透明类型。
2. **禁止** 缓存 `strmap_keys()` 返回的指针跨过变异操作——vec 扩容会使其失效。
3. **禁止** 在 `owns_value=true` 时插入非 allocator 分配的 value。
4. **禁止** 对 `strmap_remove` 返回的 value 遗忘——调用方获得所有权。
5. **禁止** 对 insert 时传入的 key 指针在 insert 后依赖其与 map 内 key 的指针等价——strmap 持有的是副本。
