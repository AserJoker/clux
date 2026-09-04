# clux core: Allocator & Memory Model

> 本文档是 `src/core/allocator.c` 的代码级约束文档，不是架构设计文档。
> 后续开发者在修改 allocator 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 allocator_t

不透明类型，持有用户提供的 `alloc_fn` 和 `free_fn` 函数指针。

```
struct _allocator_t {          // 定义在 allocator.c 内部，不暴露
    alloc_fn_t *alloc_fn;      // 分配函数（如 malloc）
    free_fn_t  *free_fn;       // 释放函数（如 free）
};
```

**规则：**
- `allocator_t` 的定义不暴露在头文件中，外部只能通过指针操作。
- 一个 `allocator_t` 实例创建后，其 `alloc_fn`/`free_fn` 不可更改。
- `delete_allocator` 不释放通过该 allocator 分配的对象——调用方须先释放所有对象。

### 1.2 class_t（运行时类型描述符）

```c
struct _class_t {
    const char *name;        // 类型名，必须比所有该类分配活得更长
    size_t      size;        // 单个对象字节数，必须 > 0
    move_fn_t   move_fn;     // 移动回调，NULL 表示不支持 move
    clone_fn_t  clone_fn;    // 克隆回调，NULL 表示不支持 clone
    dispose_fn_t dispose_fn; // 析构回调，NULL 表示无需清理
};
```

**规则：**
- `name` 指向的字符串必须比使用该 `class_t` 的所有分配活得更久。通常是字符串字面量或静态存储期变量。
- `size` 为 0 的 `class_t` 不可用于分配。
- `move_fn`/`clone_fn` 为 NULL 是合法的，表示该类型不支持对应操作。对不支持 move/clone 的类型调用 `allocator_move`/`allocator_clone` 会 panic。
- `dispose_fn` 为 NULL 是合法的，表示该类型无需清理（如 POD 类型）。

---

## 2. 内存布局

### 2.1 分配结构

每次 `allocator_new` / `allocator_new_ex` 产生的内存块由隐藏 header + 用户数据组成：

```
低地址                                                  高地址
┌──────────────┬──────────────────────────────────────────┐
│ alloc_header │ user data (count × clazz->size bytes)     │
│  clazz       │ obj[0]  obj[1]  ...  obj[count-1]        │
│  count       │                                          │
│  owns_clazz  │                                          │
└──────────────┴──────────────────────────────────────────┘
                 ^
                 │
            返回给调用方的指针
```

**header 内部结构（定义在 allocator.c，不暴露）：**

```c
typedef struct _alloc_header_t {
    class_t *clazz;       // 指向类型描述符
    size_t   count;       // 对象数量
    bool     owns_clazz;  // true 表示 clazz 由 allocator_new_ex 堆分配，free 时需释放
} alloc_header_t;
```

### 2.2 指针运算规则

- 从用户数据指针取 header：`(alloc_header_t *)((char *)data - sizeof(alloc_header_t))`
- 从 header 取用户数据指针：`(void *)((char *)header + sizeof(alloc_header_t))`
- `allocator_free` 释放的是 header 起始地址（即 `alloc_fn` 返回的原始指针），不是用户数据指针。

### 2.3 owns_clazz 规则

| 分配方式 | `owns_clazz` | `clazz` 来源 | `allocator_free` 行为 |
|----------|-------------|-------------|---------------------|
| `allocator_new` | `false` | 调用方传入的静态 `class_t*` | 不释放 clazz |
| `allocator_new_ex` | `true` | 内部 `malloc` 分配的 `class_t*` | 先 `free_fn(header)`，再 `free(clazz)` |

**约束：** `owns_clazz` 标志由 allocator 内部管理，外部不可修改。

---

## 3. 所有权模型

### 3.1 三种所有权操作

| 操作 | 函数 | 语义 | 源对象状态 |
|------|------|------|-----------|
| 分配 | `allocator_new` | 分配并零初始化 | N/A |
| 移动 | `allocator_move` | 转移资源所有权 | 被释放，指针置 NULL |
| 克隆 | `allocator_clone` | 深拷贝资源 | 保持有效 |

### 3.2 生命周期

```
allocator_new ──→ [有效对象] ──→ allocator_free ──→ [指针=NULL]
                 │
                 ├── allocator_move ──→ [新对象有效, 源=NULL]
                 │
                 └── allocator_clone ──→ [新对象有效, 源不变]
                                         └── allocator_free ──→ [各自独立释放]
```

### 3.3 双指针规则

`allocator_free`、`allocator_move`、`allocator_clone` 的数据参数使用 `void**`（双指针）：

| 函数 | 对 `*object` 的影响 |
|------|-------------------|
| `allocator_free` | 释放后置 NULL |
| `allocator_move` | 释放源后置 NULL，返回新指针 |
| `allocator_clone` | **不修改** `*object`，返回新指针 |

**约束：** 调用方应始终使用返回值更新自己的指针变量，不要缓存旧指针。

---

## 4. 回调契约

### 4.1 回调签名

所有回调接收 `allocator_t *allocator` 指针（非值），使 `allocator_t` 可保持不透明。

```c
typedef void (*dispose_fn_t)(void *self, allocator_t *allocator);
typedef void (*move_fn_t)  (void *self, allocator_t *allocator, void *another);
typedef void (*clone_fn_t) (void *self, allocator_t *allocator, void *another);
```

### 4.2 各回调的契约

**dispose_fn(self, allocator)：**
- 时机：`allocator_free` 释放内存**之前**调用。
- 语义：释放 `self` 持有的内部资源（如嵌套分配）。
- 可通过 `allocator_new`/`allocator_free` 操作嵌套内存。
- 调用后 `self` 的内存仍有效，但即将被释放，不可再使用。
- NULL 表示无需清理。

**moved-from 约束：** `allocator_move` 内部会先调用 `move_fn` 转移资源，再调用 `allocator_free` 释放源对象——此时 `dispose_fn` 会被调用。因此 `dispose_fn` 必须安全处理 moved-from 状态（即资源指针已被 `move_fn` 置 NULL 的对象）。典型做法是在 `dispose_fn` 中检查内部指针是否为 NULL，若为 NULL 则跳过释放。

```c
void my_dispose(void *self, allocator_t *allocator) {
    my_t *obj = (my_t *)self;
    if (obj->name) {             // moved-from 时 name 已被置 NULL，安全跳过
        void *p = obj->name;
        allocator_free(allocator, &p);
        obj->name = NULL;
    }
}
```

**move_fn(self, allocator, another)：**
- `self`：目标（刚分配、零初始化的对象）。
- `another`：源对象（即将被释放）。
- 语义：将 `another` 的资源所有权转移到 `self`。
- 调用后 `another` 的内容不再有意义（其内存随后被释放）。
- NULL 表示不支持 move，调用 `allocator_move` 会 panic。

**clone_fn(self, allocator, another)：**
- `self`：目标（刚分配、零初始化的对象）。
- `another`：源对象（保持有效）。
- 语义：创建 `another` 的深拷贝到 `self`。
- 调用后 `another` 必须保持不变。
- NULL 表示不支持 clone，调用 `allocator_clone` 会 panic。

### 4.3 default_move / default_clone

为 POD 类型（int、double 等平凡可拷贝类型）提供的便利函数，可直接填入 `class_t` 的回调字段。

| 函数 | 行为 |
|------|------|
| `default_move(self, allocator, another)` | `memcpy(self, another, size)` + `memset(another, 0, size)` |
| `default_clone(self, allocator, another)` | `memcpy(self, another, size)` |

**约束：**
- `default_move`/`default_clone` 不是 fallback。若 `class_t` 的 `move_fn`/`clone_fn` 为 NULL，`allocator_move`/`allocator_clone` 会 panic，不会自动调用 default 回调。
- **禁止直接调用** `default_move`/`default_clone`。它们唯一的合法用途是作为 `class_t` 的回调字段值，由 `allocator_move`/`allocator_clone` 在内部调用。直接调用属于对内部机制的误用，未来版本可能改变签名或移除，不保证兼容性。

### 4.4 回调中使用分配器

回调中需要嵌套分配/释放时，使用 `allocator_new`/`allocator_free`：

```c
/* 定义一个 byte class 用于变长缓冲区 */
static class_t byte_class = {
    .name = "byte",
    .size = 1,
    .move_fn = default_move,
    .clone_fn = default_clone,
    .dispose_fn = nullptr,
};

void my_clone(void *self, allocator_t *allocator, void *another) {
    my_t *dst = (my_t *)self;
    my_t *src = (my_t *)another;
    size_t len = strlen(src->name) + 1;
    dst->name = (char *)allocator_new(allocator, &byte_class, len);
    memcpy(dst->name, src->name, len);
}

void my_dispose(void *self, allocator_t *allocator) {
    my_t *obj = (my_t *)self;
    if (obj->name) {
        void *p = obj->name;
        allocator_free(allocator, &p);
        obj->name = nullptr;
    }
}
```

**禁止：** 不得直接访问 `allocator_t` 的内部字段。所有分配器操作必须通过公开 API 完成。

---

## 5. panic 规则

以下情况会调用 `panic()`（见 `core/panic.h`），默认行为是 `abort()`：

| 触发条件 | panic 消息格式 |
|---------|--------------|
| `allocator_new` 的 `alloc_fn` 返回 NULL | `out of memory: failed to allocate %zu bytes for '%s'` |
| `allocator_new_ex` 内部 `malloc(class_t)` 失败 | `out of memory: failed to allocate class_t for '%s'` |
| `create_allocator` 内部 `malloc` 失败 | `out of memory: failed to create allocator` |
| `allocator_move` 遇到 `move_fn == NULL` | `type '%s' does not support move` |
| `allocator_clone` 遇到 `clone_fn == NULL` | `type '%s' does not support clone` |

**设计原则：**
- OOM 是不可恢复的错误，panic 而非返回 NULL。
- 不支持 move/clone 是编程错误，panic 而非静默失败。
- 参数为 NULL（如 `allocator_new(NULL, ...)`）返回 NULL 而非 panic——这是可避免的调用方错误。

---

## 6. allocator_free 的释放顺序

```
1. 从 *data 恢复 header
2. 保存 clazz 指针和 owns_clazz 标志
3. 调用 clazz->dispose_fn(*data, allocator)   ← 对象内存仍有效
4. 调用 allocator->free_fn(header)            ← 释放整块内存
5. 若 owns_clazz，调用 free(clazz)             ← 释放 new_ex 分配的 class_t
6. *data = NULL
```

**关键约束：** 步骤 2 必须在步骤 4 之前保存 `clazz` 和 `owns_clazz`，因为步骤 4 之后 header 内存已释放，不可再访问。

---

## 7. 线程安全

- `allocator_t` 实例创建后字段不可变，多线程读取安全。
- `set_panic_handler` 修改全局状态，非线程安全。
- 用户提供的 `alloc_fn`/`free_fn` 若需在多线程中使用，必须自行保证线程安全。

---

## 8. 禁止事项

1. **禁止** 直接对 allocator 返回的用户数据指针调用 `free()`——必须使用 `allocator_free`。
2. **禁止** 对同一个指针调用两次 `allocator_free`——双指针模式已防止（置 NULL），但绕过它会触发 UB。
3. **禁止** 直接调用 `default_move`/`default_clone`——它们仅作为 `class_t` 回调字段值使用，由 allocator 内部调用。
4. **禁止** 修改 `allocator_get_class()` 返回的 `class_t*` 的字段——它是共享的运行时类型信息。
5. **禁止** 将一个 allocator 分配的对象传给另一个 allocator 释放——alloc/free 函数对必须匹配。
6. **禁止** 访问 `allocator_t` 的内部字段——它是不透明类型，所有操作通过公开 API。
7. **禁止** 使用裸 `malloc`/`calloc`/`realloc`/`free` 分配动态内存——所有内存必须走 `allocator_new`/`allocator_free`。

---

## 9. 统一内存通道

**规则：所有动态内存必须通过 `allocator_new`/`allocator_free` 分配和回收。**

不存在绕过 `class_t` 的原始内存分配接口。即使回调中需要分配变长缓冲区（如字符串），也必须定义一个 `class_t`（如 `byte_class`，`size=1`）并通过 `allocator_new(allocator, &byte_class, len)` 分配，通过 `allocator_free(allocator, &ptr)` 回收。

唯一的例外是 allocator 自身的创建（`create_allocator` 内部用 `malloc` 分配 `allocator_t` 结构体）和 `allocator_new_ex` 内部用 `malloc` 分配 `class_t`——这两处是实现层内部需求，不属于应用层代码。

此规则确保：
- 所有内存操作可被追踪和替换（通过自定义 `alloc_fn`/`free_fn`）。
- 内存统计、调试分配器、arena 分配器等可无缝接入。
- 不存在绕过 allocator 的内存泄漏。
