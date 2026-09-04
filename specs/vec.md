# clux core: Vec & Dynamic Array

> 本文档是 `src/core/vec.c` 的代码级约束文档，不是架构设计文档。
> 后续开发者在修改 vec 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 vec_t

不透明类型，管理一个 `void*` 动态数组，持有长度、容量和所有权标志。

```
struct _vec_t {                // 定义在 vec.c 内部，不暴露
    void **data;               // 指针数组（通过 allocator_new 分配）
    size_t len;                // 当前元素数量
    size_t cap;                // 已分配容量（data 数组长度）
    bool   owns_element;       // true: dispose 释放元素, clone 深拷贝元素
};
```

**规则：**
- `vec_t` 的定义不暴露在头文件中，外部只能通过指针操作。
- `owns_element` 在创建时确定，之后不可更改。
- `vec_t` 自身通过 `allocator_new` 分配（使用 `vec_class`），属于 allocator 管理的对象。

### 1.2 owns_element 语义

| `owns_element` | `vec_free` 行为 | `allocator_clone` 行为 |
|----------------|-----------------|----------------------|
| `true` | 遍历 `data[0..len-1]`，对每个非 NULL 元素调用 `allocator_free` | 遍历 `data[0..len-1]`，对每个元素调用 `allocator_clone` |
| `false` | 不释放元素，仅释放 `data` 数组 | 浅拷贝指针（`memcpy`） |

**约束：** `owns_element=true` 时，所有元素必须是由同一 `allocator_t` 分配的对象，且支持 `clone_fn`（若需克隆）。

---

## 2. 内存布局

### 2.1 vec_t 自身

`vec_t` 通过 `allocator_new(allocator, &vec_class, 1)` 分配，隐藏 header 前缀：

```
[alloc_header_t] | vec_t { data, len, cap, owns_element }
                    ^
               返回给调用方的指针
```

### 2.2 内部 data 数组

`data` 字段指向的指针数组也通过 `allocator_new` 分配，使用 `ptr_class`（`size=sizeof(void*)`）：

```
[alloc_header_t] | void* data[0] | void* data[1] | ... | void* data[cap-1]
                    ^
               vec->data 指向此处
```

**规则：** `data` 数组中 `[len, cap)` 范围的槽位为零（`NULL`），不包含有效元素。

### 2.3 两级分配关系

```
allocator_new(vec_class, 1) ──→ [header | vec_t]
                                      │
                                      └── vec_t.data ──→ allocator_new(ptr_class, cap) ──→ [header | void*[]]
                                                                                │
                                                                                └── void*[i] ──→ 用户对象 (allocator_new 分配)
```

---

## 3. 容量增长策略

### 3.1 增长公式

```
若 cap == 0:   new_cap = max(4, required)
若 cap > 0:    new_cap = max(cap * 2, required)
```

- 初始容量为 4（避免频繁重分配）
- 后续按 2 倍增长（摊还 O(1) push）
- 若 `required` 超过 2 倍当前容量，直接使用 `required`

### 3.2 重分配流程

因为没有 `realloc`，增长时必须：

1. `allocator_new(allocator, &ptr_class, new_cap)` 分配新数组
2. `memcpy` 复制 `[0, len)` 范围的指针到新数组
3. `allocator_free(allocator, &old_data)` 释放旧数组
4. 更新 `vec->data` 和 `vec->cap`

**约束：** 重分配后所有指向旧 `data` 数组的指针失效。调用方不应缓存 `vec->data` 指针（`vec_t` 不透明，正常使用不会直接访问 `data`）。

---

## 4. 回调契约

### 4.1 vec_class 回调

vec_t 作为 allocator 管理的对象，必须提供 `dispose_fn`、`move_fn`、`clone_fn`：

**vec_dispose(self, allocator)：**
1. 若 `owns_element`：遍历 `data[0..len-1]`，对每个非 NULL 元素调用 `allocator_free`
2. 若 `data` 非 NULL：调用 `allocator_free` 释放 `data` 数组
3. 置 `data=NULL`, `len=0`, `cap=0`

**vec_move_cb(self, allocator, another)：**
1. 转移 `data`、`len`、`cap`、`owns_element` 从 `another` 到 `self`
2. 置 `another->data=NULL`, `another->len=0`, `another->cap=0`

**约束：** `vec_move_cb` 不调用 `allocator_free` 释放任何元素——它只是转移指针。源 vec 的 `dispose_fn` 随后由 `allocator_free` 调用，此时 `data=NULL`，`dispose_fn` 安全跳过。

**vec_clone_cb(self, allocator, another)：**
1. 转移 `owns_element`
2. 分配新的 `data` 数组（容量 = `another->len`）
3. 若 `owns_element`：对每个元素调用 `allocator_clone`（深拷贝）
4. 若 `!owns_element`：直接复制元素指针（浅拷贝，与源共享元素）

**约束：** 不使用 `memcpy` 复制指针——所有元素通过 API 语义操作。`owns_element=true` 时，若某个元素的 `clone_fn` 为 NULL，`allocator_clone` 会 panic。

### 4.2 ptr_class

```c
static class_t ptr_class = {
    .name = "void*",
    .size = sizeof(void *),
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = NULL,
};
```

- 用于分配/释放 `data` 数组本身
- `dispose_fn = NULL`：释放数组不需要额外清理
- 不暴露给外部

---

## 5. 操作语义

### 5.1 push / pop

| 操作 | 语义 | 返回 |
|------|------|------|
| `vec_push(vec, allocator, value)` | 追加到末尾，必要时扩容 | void |
| `vec_pop(vec)` | 移除末尾元素 | 被移除的指针（caller 持有所有权） |

**约束：** `vec_pop` 返回的指针不会被释放，即使 `owns_element=true`。所有权转移给调用方。

### 5.2 insert / remove / swap_remove

| 操作 | 语义 | 复杂度 | 返回 |
|------|------|--------|------|
| `vec_insert(vec, allocator, index, value)` | 在 `index` 处插入，右移 `[index, len)` | O(n) | void |
| `vec_remove(vec, index)` | 移除 `index` 处元素，左移 `[index+1, len)` | O(n) | 被移除的指针 |
| `vec_swap_remove(vec, index)` | 用末尾元素替换 `index` 处元素 | O(1) | 被移除的指针 |

**约束：**
- `vec_insert` 的 `index > len` 时 panic
- `vec_remove` / `vec_swap_remove` 的 `index >= len` 时返回 NULL
- 返回的指针不会被释放，所有权转移给调用方

### 5.3 set

`vec_set(vec, index, value)` 替换 `index` 处的指针，返回旧指针。

**约束：** 旧指针不会被释放，即使 `owns_element=true`。调用方负责管理返回的旧指针。

---

## 6. panic 规则

| 触发条件 | panic 消息格式 |
|---------|--------------|
| `vec_insert` 的 `index > len` | `vec_insert: index %zu out of bounds (len=%zu)` |
| `vec_reserve` 容量溢出 | `vec_reserve: capacity overflow` |
| `vec_grow` 容量溢出 | `out of memory: vec capacity overflow` |
| 内部 `allocator_new` OOM | 由 allocator 的 panic 机制处理 |

---

## 7. 线程安全

- 与 `allocator_t` 一致：`vec_t` 实例不可变字段可安全多线程读取。
- 所有修改操作（push、insert、remove 等）非线程安全。
- 用户需自行保证多线程访问时的同步。

---

## 8. 禁止事项

1. **禁止** 直接操作 `vec_t` 的内部字段——它是不透明类型。
2. **禁止** 缓存 `vec_get` 返回的指针后假设索引不变——insert/remove 会移动元素。
3. **禁止** 对 `vec_pop` / `vec_remove` / `vec_swap_remove` / `vec_set` 返回的指针遗忘——调用方获得所有权，必须适时释放。
4. **禁止** 在 `owns_element=false` 的 vec 上假设元素会被释放——必须手动管理元素生命周期。
5. **禁止** 将不同 allocator 分配的元素混入 `owns_element=true` 的 vec——释放时 alloc/free 对必须匹配。
6. **禁止** 对 NULL 值调用 `vec_push`——会被静默忽略，不增加长度。
