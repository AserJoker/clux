# clux 编译器架构 — M1

## 1. Pipeline

```
clux run <file.cx>
    │
    ▼
  Driver (cmd/run → driver)
  │
  ├─ ① 加载源文件 ──→ 内存 istream（失败 → 退出码 1）
  │
  ├─ ② Lexer ──→ Token 流
  │      │  词法错误：产出 TOKEN_TYPE_ERROR 并继续；致命性由上层决定（退出码 1）
  │
  ├─ ③ Parser ──→ AST（挂 arena）
  │      │  语法错误：panic mode 恢复，收集诊断（退出码 1，不进入 sema）
  │
  ├─ ④ Semantic Analysis (顶层多遍扫描)
  │      ├─ Pass 1: Name Collection — 收集所有顶层名称(函数名)
  │      ├─ Pass 2: Type Collection — 收集函数签名(参数类型+返回类型)
  │      └─ Pass 3: Body Processing  — 处理函数体(名称解析+类型检查)
  │      │  语义错误：收集诊断（退出码 1）
  │
  ├─ ⑤ Interpreter (AST-walking) ──→ 执行结果（退出码 0）
  │      运行时错误（如 TDZ 访问）→ 退出码 2
  │
  ▼
  诊断通道：所有阶段的诊断收进公共 diag 收集器，driver 统一打印到 stderr
```

错误与退出码契约：

| 错误类别 | 恢复策略 | 退出码 |
|----------|----------|--------|
| 词法错误（Lexer） | 产出 TOKEN_TYPE_ERROR 并继续；上层决定致命性 | 1 |
| 语法错误（Parser） | panic mode 跳到语句边界，可收集多条 | 1 |
| 语义错误（Sema） | 收集诊断，不执行 | 1 |
| 运行时错误（Interp） | 立即终止 | 2 |

### 1.0 为什么需要 Driver

`cmd_run` 只负责参数解析，真正的流水线编排在独立 `driver` 模块（`include/driver/driver.h`, `src/driver/driver.c`）：

- `driver_compile_file()`：加载 + lex + parse + sema，产物是挂在 arena 上的 AST 与符号表
- `driver_run_file()`：compile + interp，返回进程退出码
- 未来 `clux build`（转译）复用 compile 段，`clux run` 复用 compile + execute 段

### 1.1 为什么需要多遍扫描

clux 没有前置声明（forward declaration），函数定义顺序无关。以下代码合法：

```
func main():i32 {
    return add(1, 2);    // add 在 main 之后定义，但可以调用
}

func add(a:i32, b:i32):i32 {
    return a + b;
}
```

因此语义分析需要对顶层做三遍扫描：

| 遍数 | 名称           | 处理内容                                           |
|------|---------------|---------------------------------------------------|
| 1    | Name Collection | 遍历顶层节点，将所有顶层名称（函数名、类型名等）注册到全局作用域（仅名称，无签名/定义） |
| 2    | Type Collection | 遍历顶层节点，将函数签名（参数类型+返回类型）和类型定义的完整信息注册到全局作用域 |
| 3    | Body Processing | 遍历函数体，执行名称解析和类型检查，此时所有顶层符号和类型已知 |

三遍扫描的好处：
- 消除前置声明的需求，函数和类型定义顺序自由
- Pass 1 快速检测重复定义
- Pass 2 建立完整的类型信息，使 Pass 3 中的函数调用能正确匹配签名
- 函数体内部仍为单遍处理（只需全局信息已就绪）
- 后续里程碑加入 struct/union 等类型定义后，Pass 1 同样收集类型名，Pass 2 处理类型内部结构

## 2. 模块职责

### 2.1 Lexer (include/parser/lexer.h, src/parser/lexer.c)

输入源文件字符流，输出 Token 流。**Lexer 只负责切分 token，不解析值**：数值的大小、转义序列的解码留给 Parser（T3/T4 的 literal 解析），token 文本是对源 buffer 的零拷贝切片。Lexer 只校验字面量的**形状**（是否闭合、后缀是否合法、转义是否被允许、字符字面量是否恰好一个字符）。

- `lexer_create(allocator_t*, istream_t*, filename)` → `lexer_t*`
- `lexer_next(lexer_t*)` → `token_t*`（调用者所有，需 `token_free`）
- 支持所有 M1 关键字、运算符、字面量（数字含进制与类型后缀、C 风格字符/字符串字面量、行/块注释）
- 每个 Token 携带源码位置（文件/行/列）
- 标识符按 Unicode ID_Start / ID_Continue 判定（ICU `u_isIDStart` / `u_isIDPart`；`_` 作为特例允许开头）
- 跳过文件开头的 UTF-8 BOM；行注释止于 CR 或 LF，CRLF 的 `\r\n` 整体归为空白
- 字面量形状校验：数字后缀白名单、转义序列白名单（含 `\xHH` 的 1-2 位 hex）、字符字面量恰好一个字符且可表示为 u8

**Token 池（上层流水线负责）**：Lexer 只暴露 `lexer_next`，不内置 peek / checkpoint / rewind / 错误状态。上层按 `lexer_next` 把 token 灌入一个普通 `vec<token*>`（`owns_element=true`，随 `vec_free` 自动释放每个 token）；Parser 按索引随机访问该池，可自由前进或回退，无需重新词法分析、无前瞻缓冲。

**词法错误由上层处理（不 fail-fast）**：遇到无法识别字符、非法数字后缀、未闭合字符串/字符/块注释等，Lexer 产出**一个** `TOKEN_TYPE_ERROR` token（错误信息通过 `token_get_error_message` 携带于该 token 上），随后**继续**词法分析（错误输入已被消费）。Lexer 自身不记录错误、不因错误停止——是否就此终止完全是流水线的决定。典型策略：Parser 拉到 `TOKEN_TYPE_ERROR` → 用 `token_get_error_message` 记入诊断 → 置 fatal → 立即终止解析。

### 2.2 Token（定义在 include/parser/lexer.h，src/parser/lexer.c）

> `token_t` 为**不透明类型**（字段私有），无独立的 `token.h` / `token.c`。

```c
typedef enum {
    TOKEN_TYPE_ERROR,      // 词法错误：不可识别输入（消息由 token_get_error_message 携带，仅一个）
    TOKEN_TYPE_IDENTIFIER,
    TOKEN_TYPE_CHARACTER,  // 字符字面量 'a'（值类型 u8；文本含引号，转义原样）
    TOKEN_TYPE_STRING,     // 字符串字面量 "abc"（文本含引号，转义原样）
    TOKEN_TYPE_NUMERIC,    // 整数/浮点字面量，含进制与类型后缀（文本为原始切片）
    TOKEN_TYPE_KEYWORD,
    TOKEN_TYPE_SYMBOL,     // 运算符与标点（maximal munch，1-2 字符）
    TOKEN_TYPE_COMMENT,           // 行注释 // ...
    TOKEN_TYPE_MULTILINE_COMMENT, // 块注释 /* */（可嵌套）
    TOKEN_TYPE_WHITESPACE,        // 合并的空白
    TOKEN_TYPE_EOF,
} token_kind_t;

typedef struct {
    token_kind_t kind;
    location_t   loc;
} token_t;  // 仅示意：真实 token_t 为不透明类型，字段私有；
            // 文本/位置/错误消息经 token_get_text / token_get_location /
            // token_get_error_message 访问。
```

Token 文本语义约定：

| 种类 | token 文本内容 |
|------|----------------|
| NUMERIC | 原始切片，如 `42`、`0xFF`、`3.14e10f64`（含后缀） |
| STRING / CHARACTER | **含引号**的原始切片，如 `"ab\n"`、`'a'`；转义序列保持原样 |
| SYMBOL | 1-2 个字符的运算符/标点 |
| COMMENT / MULTILINE_COMMENT | 含注释标记，如 `// x`、`/* y */` |

### 2.3 Parser（include/parser/parser.h, src/parser/parser.c）—— 尚未实现（T4）

递归下降解析器，Token 流 → AST。

- `parser_create(allocator_t*, lexer_t*)` → `parser_t*`
- `parser_parse(parser_t*)` → `ast_node_t*`（程序根节点）
- 表达式解析用 Pratt parsing（绑定力表驱动）
- 语法错误恢复：panic mode（跳到语句边界 `;` `}` EOF）
- **词法错误处理**：Parser 遍历 token 池时遇到 `TOKEN_TYPE_ERROR` → 用 `token_get_error_message` 记入诊断 → 置 fatal 标志 → 立即终止解析（不做 panic recovery），driver 据此直接失败
- `parser_error(parser_t*)` → 是否已发生错误（语法或词法）

### 2.4 AST (include/parser/ast.h, src/parser/ast.c) —— 尚未实现

**结构范式：公共头 + 子类化**（chibicc / lcc 范式）。所有节点共享公共头，具体节点通过 kind 区分、按子类大小分配；AST 整体挂在一个 arena 上，随编译单元释放，不做逐节点 free。

```c
typedef enum {
    // 顶层
    AST_PROGRAM,
    AST_FUNC_DEF,
    // 语句
    AST_VAR_DEF,
    AST_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_BLOCK,
    AST_EXPR_STMT,
    AST_DISCARD,        // _ = <expr> 显式丢弃返回值
    // 表达式
    AST_BINARY,
    AST_UNARY,
    AST_CALL,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_BOOL_LIT,
    AST_STRING_LIT,
    AST_CHAR_LIT,
    AST_IDENT,
    AST_CAST,         // <expr> as <type>
} ast_kind_t;

/* 公共头（所有节点首字段） */
typedef struct ast_node {
    ast_kind_t       kind;
    location_t       loc;
    struct ast_node *parent;         // 父节点（构建 AST 时填充）
    struct ast_node *next;           // 兄弟链表：语句列表 / 参数 / 实参
    struct ast_node *last;           // 兄弟链表尾节点（用于 O(1) 追加）
} ast_node_t;

/* 子类示例：变量声明 */
typedef struct {
    ast_node_t  base;
    strslice_t  name;           // 零拷贝切片（ptr + len）
    type_t     *annot;          // 显式标注的类型；NULL 表示推断
    ast_node_t *init;           // 初始化表达式；NULL 表示 undefined/TDZ
    bool        is_tdz;         // 是否处于 TDZ（var x:i32 = undefined;）
} ast_var_def_t;
```

节点分配统一走工厂：

```c
ast_node_t *ast_new(allocator_t *a, ast_kind_t kind, location_t loc);
```

赋值节点 (AST_ASSIGN) 说明：

- 赋值表达式返回 void，因此不允许连续赋值 `a = b = 1;`
- 只能作为语句使用，不能作为表达式嵌套（`=` 不在 Pratt 绑定力表中，parser 层即排除）

### 2.5 类型系统 (include/sema/type.h, src/sema/type.c) —— 尚未实现（sema/ 目录为空）

```c
typedef enum {
    TYPE_VOID, TYPE_BOOL,
    TYPE_I8, TYPE_I16, TYPE_I32, TYPE_I64,
    TYPE_U8, TYPE_U16, TYPE_U32, TYPE_U64,
    TYPE_F32, TYPE_F64,
    TYPE_STR,
    TYPE_CONST,      // const T — M1 支持
    TYPE_VOLATILE,   // volatile T — M1 忽略
    // 以下 M1 不支持，类型定义先行
    TYPE_POINTER,    // *T
    TYPE_ARRAY,      // [N]T
    TYPE_SLICE,      // []T
    TYPE_TUPLE,      // <T1, T2, ...>
    TYPE_FUNC,
} type_kind_t;

typedef struct type {
    type_kind_t  kind;
    char        *name;
    uint64_t     size;
    uint64_t     align;
    bool         is_const;      // 该类型是否被 const 修饰
} type_t;
```

基础类型单例：`type_i32()`, `type_f64()`, `type_bool()` 等。

函数类型：

```c
typedef struct {
    type_t      base;
    type_t     *param_types;
    int         param_count;
    type_t      return_type;
} func_type_t;
```

### 2.6 鸭子类型兼容性 (include/sema/type_compat.h, src/sema/type_compat.c) —— 尚未实现

M1 实现：

```c
// 两个类型布局兼容
bool type_layout_compatible(type_t a, type_t b);

// 赋值兼容（布局兼容 + 安全隐式转换）
bool type_assign_compatible(type_t target, type_t source);
```

M1 仅处理原始类型：kind 相同即布局兼容。

### 2.7 作用域 (include/sema/scope.h, src/sema/scope.c) —— 尚未实现

```c
typedef struct scope {
    struct scope *parent;
    strmap_t      names;  // name -> symbol_t
} scope_t;
```

操作：`scope_push()`, `scope_pop()`, `scope_declare()`, `scope_lookup()`。

### 2.8 语义分析 (include/sema/resolver.h, src/sema/resolver.c) —— 尚未实现

遍历 AST：
1. 名称解析：绑定标识符到声明
2. 类型检查：表达式类型推断、赋值兼容性、函数调用参数匹配
3. 结果以类型表（kind → 类型）等外部结构记录，不写入 AST 节点

### 2.9 诊断 (include/diag/diagnostic.h, src/diag/diagnostic.c) —— 尚未实现（diag/ 目录为空）

**公共模块**：Lexer、Parser、Sema、Interp 共用同一个收集器，driver 统一在出口打印，而不是边错边打。

```c
typedef enum { DIAG_ERROR, DIAG_WARNING, DIAG_NOTE } diag_level_t;

typedef struct {
    diag_level_t level;
    location_t   loc;
    char        *message;
} diagnostic_t;

typedef struct diag_buf {
    allocator_t   *alloc;
    diagnostic_t  *items;   // 动态数组
    size_t         count;
    size_t         capacity;
} diag_buf_t;

void diag_error(diag_buf_t *db, location_t loc, const char *fmt, ...);
void diag_print_all(const diag_buf_t *db);   // 统一格式化输出到 stderr
bool diag_has_error(const diag_buf_t *db);
```

输出格式：`<file>:<line>:<col>: error: <message>`。

### 2.10 解释器 (include/runtime/interp.h, src/runtime/interp.c) —— 尚未实现（无 runtime/ 目录）

AST-walking 解释器。

```c
typedef struct interp {
    allocator_t    alloc;
    // 运行时作用域栈
    // 调用栈
} interp_t;

value_t interp_run(interp_t *interp, ast_node_t *program);
value_t interp_exec(interp_t *interp, ast_node_t *node);
```

运行时值：

```c
typedef struct value {
    type_t  type;
    union {
        int64_t  int_val;
        uint64_t uint_val;
        double   float_val;
        bool     bool_val;
    };
} value_t;
```

printf 硬编码：识别 `printf` 函数名，直接调用 C 的 printf。

### 2.11 Driver（include/driver/driver.h, src/driver/driver.c）—— 尚未实现（实际 run 入口为 src/cmd/run.c）

流水线编排者，见本文档第 1 节。

```c
// 编译段：加载 + lex + parse + sema。出错返回非 0，诊断已写入 diag_buf。
int driver_compile_file(const char *path,
                        allocator_t *arena,
                        diag_buf_t  *diags,
                        ast_node_t **out_program);

// 完整流水线：compile + interp。返回进程退出码（0/1/2）。
int driver_run_file(const char *path);
```

Driver 持有编译单元的 arena，所有阶段产物（AST、符号表、strslice 指向的源 buffer）在同一生命周期内有效。

## 3. 命令行接口

```
clux run <file.cx>    解释执行 .cx 文件
```

退出码：0 成功，1 编译错误（含文件打不开、词法/语法/语义错误），2 运行时错误。

## 4. 构建系统

CMake（C11；测试为 C++20 + GoogleTest）。库划分：

| 目标 | 源文件 | 说明 |
|------|--------|------|
| `clux_core` | `src/core/*.c` | allocator / stream / vec / rbtree / omap / strmap / string |
| `clux_parser` | `src/parser/*.c` | lexer（T4 之后加入 parser.c），依赖 `clux_core` |
| `clux_sema` | `src/sema/*.c` | 类型系统与语义分析；**目录为空时不创建目标**（GLOB + `if`） |
| `clux_diag` | `src/diag/*.c` | 诊断收集器；同上 |
| `clux_cmd` | `src/cmd/*.c` | 子命令分发 |

`sema` / `diag` 两个目录目前还没有源文件（对应流水线阶段 T5/T6），一旦放入第一个 `*.c`，CMake 会自动创建对应目标并链入 `clux` 与 `clux_test`，无需再改构建脚本。测试目标通过 `file(GLOB CONFIGURE_DEPENDS)` 自动收集 `tests/*.cpp`。

**编译警告**：项目目标统一启用严格警告（MSVC 风格驱动用 `/W4`，GNU 风格驱动用 `-Wall -Wextra -Wpedantic`），第三方（ICU / GoogleTest）保持各自配置——通过 `clux_target_warnings(<target>)` 施加，不用全局 `add_compile_options`。

刻意关闭/绕过的项：

| 项 | 处理 | 理由 |
|------|------|------|
| MSVC CRT 安全弃用（`fopen` / `tmpnam` / `freopen`） | Windows 下定义 `_CRT_SECURE_NO_WARNINGS` | clux 使用可移植 C API，而非 MSVC 专有的 `_s` 变体 |

其余警告视为真实缺陷并直接修掉（如未使用变量/参数、C99 compound literal、结构体部分初始化），**当前构建零警告**。

### 4.1 构建注意事项

**不要执行 `ninja clean` 或 `cmake --build <dir> --clean-first`。**

`third_party/icu/icu_data_gen.c` 是在 **configure 阶段**生成到 build 目录中的文件，clean 会把它删掉，而 ninja 没有重建它的规则，随后构建必然失败：

```
FAILED: third_party/icu/CMakeFiles/icudata.dir/icu_data_gen.c.obj
clang: error: no such file or directory: '<build>/third_party/icu/icu_data_gen.c'
```

- 已经 clean 过：重新 configure 即可恢复（`cmake -S . -B build`，或在 build 目录内执行 `cmake .`），再正常构建。
- 需要一次干净构建：另建一个 build 目录（`cmake -S . -B build-clean && cmake --build build-clean`），而不是清理原目录。

## 5. 测试策略

- **单元测试**：Lexer、Parser、类型系统各模块独立测试
- **集成测试**：.cx 程序端到端执行，对比输出
- **测试用例**：hello.cx、arithmetic.cx、functions.cx、control_flow.cx、fibonacci.cx
