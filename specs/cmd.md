# clux cmd: 子命令分发框架

> 本文档是 `src/cmd/cmd.c` 及各子命令实现的代码级约束文档。
> 后续开发者在修改 cmd 模块或新增子命令前必须阅读并遵守本文档中的规则。

---

## 1. 核心抽象

### 1.1 cmd_t

```
typedef struct {
    const char *name;        // 子命令名称，如 "build"
    const char *usage;       // 一行用法，如 "clux build [options]"
    const char *help;        // 多行描述，--help 时打印
    cmd_handler_fn_t handler; // 命令处理函数
} cmd_t;
```

命令表为 `static const cmd_t[]` 数组，定义在 `main.c` 中。新增子命令只需：
1. 在 `src/cmd/xxx.c` + `include/cmd/xxx.h` 中实现 `cmd_xxx`
2. 在 `main.c` 中 `#include "cmd/xxx.h"` 并在命令表中添加条目

### 1.2 cmd_args_t

```
typedef struct {
    const cmd_opt_t *opts;  // 解析后的选项数组
    size_t optc;            // 选项数量
    char *const *posargs;   // 位置参数（指向 argv 原始槽位）
    size_t posc;            // 位置参数数量
} cmd_args_t;
```

**零堆分配：** 所有指针指向原始 argv 字符串，opts 数组写入调用者提供的栈上 buffer。

### 1.3 cmd_opt_t

```
typedef struct {
    const char *key;    // 不含 "--" 前缀，如 "verbose" 对应 --verbose
    const char *value;  // NULL 表示 flag，否则为 = 后的值字符串
} cmd_opt_t;
```

对于 `--key=value`，原地将 `=` 替换为 `\0`（argv 字符串按 C 标准可修改），key 和 value 分别指向分割后的两段。

---

## 2. 参数解析

### 2.1 cmd_args_parse

```
size_t cmd_args_parse(int argc, char **argv, cmd_opt_t *opt_buf,
                      size_t opt_buf_len, cmd_args_t *out);
```

识别规则：
- `--key=value` → opts[i] = {key="key", value="value"}
- `--key`       → opts[i] = {key="key", value=NULL}
- `--`          → 跳过（bare separator）
- 其他          → 位置参数

**argv 修改：** 对于 `--key=value`，原地将 `=` 替换为 `\0`。调用方必须意识到 argv 内容被修改。

**opt_buf 溢出：** 若选项数量超过 `opt_buf_len`，超出部分被静默丢弃。

### 2.2 位置参数

位置参数通过 `posargs` 和 `posc` 访问。当没有选项时，`posargs` 指向 argv[0]，`posc` 等于 argc。当有选项时，`posargs` 指向第一个位置参数。

**注意：** 当前实现要求位置参数出现在所有选项之后。混合排列时，选项前的位置参数会被计入 posc 但 pos_start 可能不正确。

---

## 3. 分发流程

### 3.1 cmd_dispatch

```
int cmd_dispatch(const cmd_t *cmds, size_t ncmds, int argc, char **argv);
```

1. `argc < 2`：打印顶层帮助，返回 1
2. `argv[1]` 为 `--help` 或 `-h`：打印顶层帮助，返回 0
3. 在 cmds 中查找 `argv[1]` 匹配的子命令
4. 未找到：报错并提示 `--help`，返回 1
5. 解析 `argv[2..]` 为 `cmd_args_t`（栈上 `opt_buf[32]`）
6. 若子命令收到 `--help`：打印子命令帮助，返回 0
7. 调用 `cmd->handler(&args)`

### 3.2 cmd_print_help

```
void cmd_print_help(const char *prog, const cmd_t *cmds, size_t ncmds);
```

输出格式：
```
Usage: <prog> <command> [options]

Commands:
  <name>       <usage>
  ...

Options:
  --help       Show this help message
```

---

## 4. 子命令文件组织

```
include/cmd/   cmd.h, format.h, build.h, run.h, test.h, version.h
src/cmd/       cmd.c, format.c, build.c, run.c, test.c, version.c
```

### 4.1 约定

- 每个子命令一个 `.c`/`.h` 对，文件名与命令名一致
- `.h` 仅声明 `int cmd_xxx(const cmd_args_t *args);`
- `.c` 实现 handler，`#include "cmd/xxx.h"`
- handler 返回 0 表示成功，非零表示失败
- 占位命令输出 `<name>: not implemented` 到 stderr 并返回 1

### 4.2 CMake

子命令源文件加入 `clux_cmd` 静态库。需要 ICU 的子命令（如 version）通过 `clux_cmd` 的 `target_link_libraries` 获取。

---

## 5. 查找函数

### 5.1 cmd_args_get

```
const char *cmd_args_get(const cmd_args_t *args, const char *key);
```

线性搜索 opts 数组，返回第一个匹配 key 的 value。key 不存在或为 flag（value=NULL）时返回 NULL。

**限制：** 无法区分"key 不存在"和"key 存在但是 flag"——需用 `cmd_args_has` 判断存在性。

### 5.2 cmd_args_has

```
bool cmd_args_has(const cmd_args_t *args, const char *key);
```

线性搜索 opts 数组，返回 key 是否存在（不论是否有 value）。

### 5.3 cmd_args_pos

```
const char *cmd_args_pos(const cmd_args_t *args, size_t i);
```

返回第 i 个位置参数，越界返回 NULL。

---

## 6. 禁止事项

1. **禁止** 依赖 argv 内容在 `cmd_args_parse` 后保持原样——`--key=value` 中的 `=` 被替换为 `\0`。
2. **禁止** 在 handler 中使用 malloc/free——遵循项目统一内存通道规则。
3. **禁止** 缓存 `cmd_args_t` 的指针超出 handler 生命周期——opts 指向栈上 buffer。
4. **禁止** 在 opt_buf 溢出时假设所有选项都被解析——超过 32 个选项时静默截断。
5. **禁止** 混合排列选项和位置参数——当前解析器不保证正确处理 `posarg --opt posarg` 的顺序。
