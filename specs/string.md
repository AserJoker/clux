# clux core: String 容器

> 本文档是 `src/core/string.c` 及 `include/core/string.h` 的代码级约束文档。
> 后续开发者在修改 string 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 string_t

不透明类型，封装一个可增长的字节缓冲区，内部持有 allocator 引用。

```
struct _string_t {                 // 定义在 string.c 内部，不暴露
    char           *data;         // NUL 结尾的字节缓冲（allocator 管理）
    size_t          len;          // 字节长度，不含 NUL 结尾符
    size_t          cap;          // 已分配字节数（含 NUL 槽）
    allocator_t    *allocator;    // 内部操作使用的分配器
};
```

**规则：**
- `string_t` 的定义不暴露在头文件中，外部只能通过指针操作。
- **allocator 内部持有**：构造时传入 allocator，之后所有原地修改/释放操作**不再需要 allocator 参数**（对齐 `stream.md` 的重构方向——"分配器封装在对象内部"）。
- `data` 始终 NUL 结尾（`data[len] == '\0'`），但允许嵌入 NUL（`len` 是权威长度，`string_from_bytes`/`string_append_bytes` 按字节处理）。
- 空字符串的 `data` 可以为 NULL（`cap==0`），此时 `string_cstr` 返回静态字面量 `""`。

### 1.2 与 vec 的差异

| 特性 | vec | string |
|------|-----|--------|
| 元素类型 | `void*` | 字节 |
| 内部 allocator | 不持有（操作需传 allocator） | **持有**（操作不需传） |
| 释放函数签名 | `vec_free(allocator, &v)` | `string_free(&s)` |
| 特殊语义 | 所有权转移 | NUL 结尾 + 字节级操作 |

---

## 2. 内存布局

### 2.1 两级分配关系

```
allocator_new(string_class, 1) ──→ [header | string_t]
                                          │
                                          └── string_t.data ──→ allocator_new(char_class, cap) ──→ [header | char[0..cap-1]]
                                                                                                      │
                                                                                                      └── data[cap-1] = '\0'（始终）
```

- `string_t` 自身通过 `allocator_new(allocator, &string_class, 1)` 分配。
- `data` 缓冲通过 `allocator_new(allocator, &char_class, cap)` 分配（`char_class`：`size=1`，内部静态，不暴露）。
- `cap` 始终 >= `len + 1`（NUL 槽）。

### 2.2 容量增长策略

```
若 cap == 0:   new_cap = max(4, required)
若 cap > 0:    new_cap = max(cap * 2, required)
```

- 与 vec 相同：初始 4，按 2 倍增长（摊还 O(1) append）。
- 无 realloc：增长时新分配 + memcpy + 释放旧缓冲。
- `required` 是**含 NUL 槽**的总字节数（调用处传入 `len + additional + 1`）。

---

## 3. 操作语义

### 3.1 构造/析构

| 函数 | 语义 |
|------|------|
| `string_new(allocator)` | 空字符串（data=NULL, cap=0） |
| `string_from_cstr(allocator, cstr)` | 复制 NUL 结尾 C 字符串 |
| `string_from_bytes(allocator, data, len)` | 复制 len 字节（允许嵌入 NUL） |
| `string_from_string(allocator, other)` | 独立深拷贝 |
| `string_free(&s)` | 使用内部 allocator 释放，双指针置 NULL |

### 3.2 修改（原地，使用内部 allocator）

| 函数 | 语义 |
|------|------|
| `string_append_cstr/bytes/char/string` | 追加，必要时扩容 |
| `string_assign_cstr/bytes` | 清空后写入 |
| `string_clear` | 置空，保留容量 |
| `string_reserve(additional)` | 预扩容至少 len+additional+1 |
| `string_shrink_to_fit` | 收缩到 len+1 |

**约束：**
- append 系列容量检查：`len + add + 1` 溢出时 panic（`string: capacity overflow`）。
- `string_reserve` 溢出时 panic（`string_reserve: capacity overflow`）。
- `string_clear` 不释放缓冲，容量保留。

### 3.3 搜索

| 函数 | 语义 |
|------|------|
| `string_find(str, needle, start)` | 字节级首次匹配（>= start），未找到返回 `STRING_NPOS` |
| `string_rfind(str, needle)` | 字节级最后一次匹配 |
| `string_contains` / `string_starts_with` / `string_ends_with` | 布尔判断 |

**空 needle 语义（对齐 C++）：**
- `find("")` 返回 `start`（若 start <= len）
- `rfind("")` 返回 `len`
- `starts_with("")` / `ends_with("")` 返回 true

**约束：** 搜索是**字节级**的（内部 `mem_search`，类似 memmem），不感知 UTF-8 码点边界——调用方需自行保证按码点搜索时传入的 needle 对齐边界。

### 3.4 比较

- `string_compare(a, b)`：strcmp 语义（memcmp + 长度比较）。
- NULL 排序：NULL < 任何非 NULL；NULL vs NULL 返回 0。
- `string_equals(a, b)`：长度相同且内容相同。

### 3.5 派生（返回新 string，原对象不变）

| 函数 | 语义 |
|------|------|
| `string_substring(allocator, str, start, len)` | 截取；len 截断到可用字节；start 越界返回 NULL |
| `string_concat(allocator, a, b)` | 拼接，长度溢出 panic |
| `string_replace(allocator, str, needle, replacement)` | 替换第一个匹配 |
| `string_replace_all(allocator, str, needle, replacement)` | 替换所有匹配 |

**替换语义：**
- `replacement == NULL` 视为空串（即删除匹配）。
- `needle == NULL` 返回 NULL（非法参数）；`needle` 为空串返回 str 的副本（无匹配可替换）。
- 无匹配时返回 str 的深拷贝（新对象，与原对象独立）。
- `replace_all` 先计数匹配、计算新长度（`len + count*(rlen-nlen)`，仅 rlen>nlen 时检查溢出），再一次性构造，避免多次扩容。
- 替换循环中匹配不重叠：每次匹配后从 `i + nlen` 继续搜索。

---

## 4. 回调契约

### 4.1 string_class 回调

**string_dispose(self, allocator)：**
1. 若 `data` 非 NULL：`allocator_free(str->allocator, &data)`（data 由内部 allocator 分配）
2. 置 `data=NULL, len=0, cap=0, allocator=NULL`

**约束：** move 后源的 dispose 安全（data 已置 NULL，跳过释放）。

**string_move_cb(self, allocator, another)：**
1. 转移 `data/len/cap/allocator` 从 another 到 self
2. 置源字段为 NULL

**string_clone_cb(self, allocator, another)：**
1. `self->allocator = allocator`（传入的 allocator）
2. 若 `src->len > 0`：分配 `len+1` 字节，memcpy 内容（含 NUL）
3. 空字符串克隆后 data=NULL, cap=0

---

## 5. 访问器语义

| 函数 | 返回 |
|------|------|
| `string_cstr` / `string_data` | NUL 结尾 C 字符串；空串返回 `""`（静态字面量）；**只读** |
| `string_len` | 字节长度（不含 NUL） |
| `string_cap` | 已分配字节数（含 NUL 槽） |
| `string_char_at(str, index)` | `unsigned char` 值（0-255）；越界返回 -1 |

**约束：** `string_cstr` 返回的指针在下次修改操作后可能失效（扩容导致重分配），不可跨操作缓存。

---

## 6. panic 规则

| 触发条件 | panic 消息格式 |
|---------|--------------|
| `string_append_bytes` 容量溢出 | `string: capacity overflow` |
| `string_reserve` 容量溢出 | `string_reserve: capacity overflow` |
| `string_concat` 长度溢出 | `string_concat: length overflow` |
| `string_replace` 长度溢出 | `string_replace: length overflow` |
| `string_replace_all` 长度溢出 | `string_replace_all: length overflow` |
| 内部 `allocator_new` OOM | 由 allocator 的 panic 机制处理 |

**设计原则：**
- OOM/容量溢出是不可恢复错误，panic。
- 参数为 NULL 返回 NULL / no-op / 哨兵值，不 panic——遵循项目 NULL 防御惯例。
- 所有公开函数对 NULL 输入做防御（返回 NULL / 0 / `STRING_NPOS` / `""` / no-op）。

---

## 7. 线程安全

- `string_t` 的只读操作（`string_cstr`/`string_len`/`string_find` 等）可安全多线程读取。
- 所有修改操作非线程安全。
- 用户需自行保证多线程访问时的同步。

---

## 8. 禁止事项

1. **禁止** 直接操作 `string_t` 的内部字段——它是不透明类型。
2. **禁止** 对 `string_cstr` 返回的指针做修改或跨修改操作缓存——扩容会使其失效。
3. **禁止** 绕过 allocator 直接 `malloc`/`free`——所有动态内存必须走 `allocator_new`/`allocator_free`（统一内存通道）。
4. **禁止** 将 `replacement` 参数与 `needle` 参数混淆——`replacement==NULL` 是合法语义（删除匹配），`needle==NULL` 是非法参数（返回 NULL）。
5. **禁止** 假设 `string_cstr` 返回的指针在 `string_append_*` 后仍指向同一地址。
6. **禁止** 直接调用 `default_move`/`default_clone`——它们仅作为 `char_class` 回调字段值使用。
7. **禁止** 用 `strlen` 代替 `string_len` 计算长度——字符串可能嵌入 NUL，`strlen` 会提前终止。
