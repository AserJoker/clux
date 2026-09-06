# clux driver: 脚本执行流水线

> 本文档是 `src/driver/driver.c` 与 `src/cmd/run.c` 的代码级约束文档。
> 后续开发者在修改流水线编排或新增子命令入口前必须阅读并遵守本文档中的规则。

---

## 1. 定位与职责

`driver` 是 M1 执行流水线的**唯一编排者**。`cmd_run` 只负责参数解析（`cmd_args_t`），随后调用 `driver_run_file(path)` 并原样返回退出码。

```
cmd_run(args)
  └─ driver_run_file(path)
       ├─ driver_compile_file(path, arena, diags, &program)
       │    ① 加载源文件 → 内存 istream
       │    ② lexer_create + parser_parse → AST（挂在 arena）
       │    ③ 词法错误（fail-fast）或语法错误（panic 恢复）→ 诊断入 diag_buf
       │    ④ 无错误 → sema 三遍扫描
       ├─ 编译错误（diag_has_error）→ diag_print_all → 返回 1
       └─ interp_run → 返回 0；运行时错误 → 返回 2
```

退出码契约（进程级，`clux` 二进制的返回值）：

| 值 | 含义 |
|----|------|
| 0 | 成功 |
| 1 | 编译错误：文件打不开、词法/语法/语义错误 |
| 2 | 运行时错误 |

## 2. API

```c
// include/driver/driver.h
int driver_compile_file(const char *path,
                        allocator_t *arena,
                        diag_buf_t  *diags,
                        ast_node_t **out_program);

int driver_run_file(const char *path);
```

### 2.1 driver_compile_file

- 打开 `path` 读取全文。**文件 I/O 失败不 panic**：向 `diags` 写入一条错误诊断并返回非 0。
- Lexer 需要直接数据访问（`istream_data != NULL`）的 mem-backed 源：文件内容必须读入内存后 `stream_source_mem` 包装，不能直接把 file-backed source 传给 lexer。
- `filename` 指针由调用方持有（指向 `path` 参数或 arena 内副本），必须活过整个编译单元。
- 任何错误下都返回非 0，且 `*out_program` 不保证有效（可置 NULL）；成功时返回 0。

### 2.2 driver_run_file

- 创建编译单元 arena（`create_allocator` 或项目统一分配器入口）与 `diag_buf_t`。
- 编译错误 → 打印诊断 → 销毁 arena → 返回 1。
- 语义分析通过 → `interp_run`。运行时错误 → 打印诊断 → 返回 2。成功 → 0。
- **arena 生命周期**：AST、符号表、strslice 指向的源 buffer 全部挂在编译单元 arena 上，`driver_run_file` 返回前统一释放。禁止在中间阶段手动 free 任何 AST 节点。

## 3. 加载源文件

```
加载失败（文件不存在 / 无法读取）→ 诊断：cannot open file '<path>' → 退出码 1
```

约束：
- 二进制读取（`"rb"`），大小上限不做特殊处理（M1 规模）。
- 读入内存后创建 `stream_source_mem(allocator, data, size, owns_data=true)`；`owns_data` 归 stream 管理，随 istream 关闭释放。

## 4. 错误处理规则

### 4.1 词法错误（fail-fast，不可恢复）

- Lexer 遇到错误 → 产出 `TOKEN_TYPE_ERROR`，之后所有调用返回 EOF，`lexer_error` 返回消息。
- Parser 在拉取 token 时看到 `TOKEN_TYPE_ERROR` → 把 `lexer_error` 的消息作为**错误诊断**记入 `diag_buf` → 置 fatal → **立即终止解析**（不执行 panic recovery、不继续收集）。
- Driver 检查到 fatal（parser_error 或 diag_has_error）→ 打印诊断 → 退出码 1。
- 禁止：对词法错误做跳过/恢复/重试。

### 4.2 语法错误（panic mode，可恢复）

- Parser 的 `expect` 失败 → 记录诊断 → 跳到语句边界（`;` `}` EOF）→ 继续解析，可收集多条。
- 只要存在语法错误，driver **不进入 sema**（避免级联误报）。

### 4.3 语义错误

- sema 三遍扫描期间的错误全部写入 `diag_buf`，扫描结束后统一判断。
- 有语义错误 → 不执行 → 退出码 1。

### 4.4 运行时错误

- Interp 遇到运行时错误（如 TDZ 变量非赋值访问、除零）→ 立即终止 → 退出码 2。

## 5. 诊断输出

统一由 driver 在退出前调用 `diag_print_all`，格式：

```
<file>:<line>:<col>: error: <message>
```

禁止各阶段自行向 stderr 打印（`panic` 级别崩溃除外）。

## 6. 与子命令的关系

- `clux run` = `driver_compile_file` + `interp_run`。
- 未来 `clux build` 复用 `driver_compile_file`（编译段），`clux format` 等不经过 driver。

## 7. 禁止事项

1. **禁止** `cmd_run` 直接调用 lexer/parser/sema/interp——必须走 driver 入口。
2. **禁止** 在 driver 之外持有 AST 指针越过 `driver_run_file` 返回点（arena 已释放）。
3. **禁止** 对词法错误做恢复处理（fail-fast 是硬约束）。
4. **禁止** 在编译错误存在时仍执行 interp。
5. **禁止** 在 driver 内部用 `printf` 直接输出诊断（走 `diag_print_all`）。
