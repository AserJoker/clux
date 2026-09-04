# clux core/stream: UTF-8 字符流

> 本文档是 `src/core/stream.c` 及 `include/core/stream.h` 的代码级约束文档。
> 后续开发者在修改 stream 模块前必须阅读并遵守本文档中的规则。

---

## 1. 核心架构

### 1.1 数据来源/目标外包

Stream 的字节级 I/O 操作通过 `stream_source_t`（读）和 `stream_sink_t`（写）vtable 全局外包，stream 本身仅负责：
- UTF-8 码点级编解码
- 行/列/字符簇位置追踪
- 格式化读写（printf/scanf）

### 1.2 stream_source_t（读后端）

```c
typedef struct {
    void *ctx;
    size_t (*read)(void *ctx, void *buf, size_t len);
    int    (*seek)(void *ctx, size_t offset);
    size_t (*tell)(const void *ctx);
    const char *(*data)(const void *ctx);   // 可选：直接访问源数据
    size_t (*size)(const void *ctx);         // 源数据总大小
    void   (*close)(void *ctx);
} stream_source_t;
```

- 值类型（struct 含函数指针 + ctx 指针），可栈分配
- `ctx` 指向后端状态（由工厂函数堆分配，close 时释放）
- `read`：从当前位置读取最多 `len` 字节到 `buf`，返回实际读取数，0=EOF
- `seek`：跳转到绝对偏移，返回 0 成功，-1 不支持
- `tell`：返回当前字节偏移
- `data`：返回源数据指针（可选，不支持返回 NULL）
- `size`：返回源数据总字节数（不支持返回 0）
- `close`：释放 ctx 资源

### 1.3 stream_sink_t（写后端）

```c
typedef struct {
    void *ctx;
    size_t (*write)(void *ctx, const void *data, size_t len);
    int    (*seek)(void *ctx, size_t offset);
    size_t (*tell)(const void *ctx);
    const char *(*data)(const void *ctx);   // 可选：直接访问已写数据
    void   (*reset)(void *ctx);
    void   (*close)(void *ctx);
} stream_sink_t;
```

- `write`：写入 `len` 字节，返回实际写入数
- `reset`：重置到初始状态（如清空缓冲区，保留容量）

### 1.4 stream_pos_t

```c
typedef struct {
    size_t byte_offset;  // 0-based 绝对字节偏移
    size_t line;         // 1-based 行号
    size_t col;          // 1-based 码点列号
    size_t cluster_col;  // 1-based 字符簇列号
} stream_pos_t;
```

---

## 2. 内置后端

### 2.1 stream_source_mem

```c
stream_source_t stream_source_mem(allocator_t *alloc, const char *data,
                                  size_t len, bool owns_data);
```

- 从现有内存缓冲区创建读后端
- `owns_data=true` 时，close 通过 allocator 释放 data
- `owns_data=false` 时，调用者保证 data 生命周期超过 source
- 内部状态：`mem_source_ctx_t`（data, len, pos, owns_data, alloc）

### 2.2 stream_sink_mem

```c
stream_sink_t stream_sink_mem(allocator_t *alloc);
```

- 创建写入可增长缓冲区的写后端
- 2x 增长策略，初始容量 64
- 内部状态：`mem_sink_ctx_t`（buf, len, cap, alloc）

访问器：
```c
const char *stream_sink_mem_data(const stream_sink_t *sink);
size_t stream_sink_mem_size(const stream_sink_t *sink);
```

### 2.3 stream_source_file

```c
stream_source_t stream_source_file(allocator_t *alloc, const char *path);
```

- 从文件路径创建读后端
- 以二进制读模式（"rb"）打开文件
- 可 seek，`size()` 返回文件大小（构造时缓存）
- `data()` 返回 NULL（不支持直接访问）
- close 时 fclose 并释放 ctx
- 失败（路径为 NULL 或文件无法打开）返回零初始化 source

### 2.4 stream_source_file_fp

```c
stream_source_t stream_source_file_fp(allocator_t *alloc, FILE *fp, bool owns_fp);
```

- 包装已有 FILE* 创建读后端
- FILE* 必须可 seek 且以二进制模式打开
- `owns_fp=true` 时 close 会 fclose
- 内部状态：`file_source_ctx_t`（fp, file_size, owns_fp, alloc）
- 失败（alloc 或 fp 为 NULL）返回零初始化 source

### 2.5 stream_sink_file

```c
stream_sink_t stream_sink_file(allocator_t *alloc, const char *path);
```

- 从文件路径创建写后端
- 以二进制写模式（"wb"）打开文件，创建或截断
- `data()` 返回 NULL（不支持直接访问）
- `reset()` 仅 fseek 到文件开头（不截断）
- close 时 fclose 并释放 ctx
- 失败（路径为 NULL 或文件无法打开）返回零初始化 sink

### 2.6 stream_sink_file_fp

```c
stream_sink_t stream_sink_file_fp(allocator_t *alloc, FILE *fp, bool owns_fp);
```

- 包装已有 FILE* 创建写后端
- `owns_fp=true` 时 close 会 fclose
- 内部状态：`file_sink_ctx_t`（fp, owns_fp, alloc）
- 失败（alloc 或 fp 为 NULL）返回零初始化 sink

---

## 3. istream_t（读流）

### 3.1 内部结构

```c
struct _istream_t {
    stream_source_t source;
    allocator_t *allocator;
    char *line_buf;        // 当前行内容（line_start 到 pos）
    size_t line_buf_cap;
    size_t line_buf_len;
    size_t pos;            // 绝对字节偏移
    size_t line;           // 1-based
    size_t col;            // 1-based 码点列
    size_t line_start;     // 绝对字节偏移
};
```

### 3.2 行缓冲

- `line_buf` 存储从 `line_start` 到当前 `pos` 的字节
- 用于 `istream_tell` 中的 `cluster_col` 计算（ICU break iterator 需要行内容）
- 读到码点时追加到 line_buf；遇到行断时清空
- `istream_seek` 时从源重读行内容填充 line_buf

### 3.3 码点读取流程

`istream_read_cp`:
1. 调用 `source_read_cp` 从源读取码点（seek→read lead→read continuation→decode）
2. 追加码点字节到 line_buf
3. 更新 pos/line/col/line_start
4. CR+LF 处理：读 CR 后尝试读取下一个码点，如果是 LF 则一并消费，否则 seek 回

`istream_peek_cp`:
1. 从当前 pos 读取码点
2. seek 回原位置
3. 不修改任何状态

### 3.4 位置追踪

- `col` 增量维护：每读一个非行断码点 col++
- `cluster_col` 惰性计算：`istream_tell` 中从 line_buf 通过 ICU break iterator 计算
- `istream_seek`：O(n) 扫描从 byte 0 到目标偏移重建 line/col/line_start

### 3.5 构造/析构

```c
istream_t *istream_open(allocator_t *alloc, stream_source_t source);
void istream_close(istream_t **stream);
```

- `istream_open` 接管 source 所有权，析构时调用 `source.close`
- `istream_close` 使用内部 allocator 释放 line_buf 和 stream 本身
- 不再需要外部传入 allocator 参数

---

## 4. ostream_t（写流）

### 4.1 内部结构

```c
struct _ostream_t {
    stream_sink_t sink;
    allocator_t *allocator;
    size_t line;
    size_t col;
    size_t line_start;
    char *line_buf;        // 当前行内容
    size_t line_buf_cap;
    size_t line_buf_len;
};
```

### 4.2 写入流程

- `ostream_write_cp`：编码码点→sink.write→追加 line_buf→更新位置
- `ostream_write`：sink.write→ostream_update_pos
- `ostream_printf`：格式化到临时缓冲区→ostream_write→释放临时缓冲区
- 写操作不再需要 `allocator_t *` 参数（分配器封装在 sink 中）

### 4.3 位置追踪

`ostream_update_pos`：
- 扫描写入数据中的行断更新 line/col/line_start
- `write_start` 参数 = sink.tell()（写入前的位置）
- 行断后重置 line_buf，只保留最后一段行内容

### 4.4 重置

`ostream_reset`：调用 `sink.reset` 清空缓冲区，重置 line/col/line_start/line_buf_len。

---

## 5. 行断检测

### 5.1 行断码点

```
U+000A  LF
U+000D  CR（若后接 LF 则合并为单一行断）
U+2028  Line Separator
U+2029  Paragraph Separator
```

### 5.2 CR+LF 处理

- istream：读 CR 后读取下一码点，若是 LF 则跳过 LF（一并消费）
- ostream：写入数据中遇到 CR+LF 序列时视为单一行断

---

## 6. 字符簇列计算

### 6.1 惰性计算

`cluster_col` 不存储在 struct 中，在 `tell()` 时从 `line_buf` 通过 ICU break iterator 计算。

### 6.2 compute_cluster_col

1. 将 line_buf 中的 UTF-8 片段转为 UTF-16
2. 用 `ubrk_open(UBRK_CHARACTER, NULL, ...)` 创建 break iterator
3. 遍历 boundary 计数，返回 1-based cluster_col

### 6.3 性能特征

- 每次 `tell()` 调用都会 malloc UTF-16 缓冲区 → u_strFromUTF8 → ubrk_open → 遍历 → ubrk_close → free
- 对于短行（常见场景），开销可接受
- 后续可引入缓存优化

---

## 7. ICU 依赖

- 程序启动时必须调用 `icu_data_init()` 注册嵌入的 ICU 数据
- 否则 `ubrk_open` 返回 `U_ILLEGAL_ARGUMENT_ERROR`
- 测试入口 `tests/main.cpp` 中已集成初始化

---

## 8. API 变更对照

| 旧 API | 新 API | 变更说明 |
|--------|--------|---------|
| `istream_new(alloc, data, len, owns)` | `istream_open(alloc, stream_source_mem(alloc, data, len, owns))` | 数据源外包 |
| `istream_free(alloc, &s)` | `istream_close(&s)` | 不再需外部 allocator |
| `ostream_new(alloc)` | `ostream_open(alloc, stream_sink_mem(alloc))` | 数据槽外包 |
| `ostream_free(alloc, &s)` | `ostream_close(&s)` | 不再需外部 allocator |
| `ostream_write_cp(s, alloc, cp)` | `ostream_write_cp(s, cp)` | 移除 allocator 参数 |
| `ostream_write(s, alloc, data, len)` | `ostream_write(s, data, len)` | 移除 allocator 参数 |
| `ostream_printf(s, alloc, fmt, ...)` | `ostream_printf(s, fmt, ...)` | 移除 allocator 参数 |

---

## 9. NULL 安全

所有公开函数对 NULL 输入做防御：
- 返回指针的函数：返回 NULL
- 返回数值的函数：返回 0 或 -1
- 返回 stream_pos_t 的函数：返回 `{0, 1, 1, 1}`
- void 函数：no-op
- `istream_close`/`ostream_close` 对 NULL 或 *NULL 做 no-op

---

## 10. 禁止事项

1. **禁止** 在 struct 中增量维护 cluster_col——必须通过 ICU break iterator 惰性计算
2. **禁止** 绕过 source/sink vtable 直接操作后端内部状态
3. **禁止** 忘记在程序启动时调用 `icu_data_init()`
4. **禁止** 在 `istream_scanf` 后假设 pos 已自动推进——必须手动 seek 或 read
5. **禁止** 对 ostream_write 传入非 UTF-8 数据
6. **禁止** 在 istream_close 后继续使用 source（close 已释放 ctx）
7. **禁止** 在 ostream_close 后继续使用 sink（close 已释放 ctx）
8. **禁止** 假设非 seekable source 支持所有操作（peek/seek/scanf 可能失败）
