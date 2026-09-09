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
  ├─ ④ Semantic Analysis (shadow value 驱动)
  │      │  先构建 sema 作用域树（scope 节点带完整符号表），
  │      │  再按作用域树用 shadow value 遍历 AST 做类型检查与推导。
  │      │  所有类型验证通过后才进入字节码编译。
  │      ├─ Pass 1: Name Collection — 收集所有顶层名称(函数名)
  │      ├─ Pass 2: Type Collection — 收集函数签名(参数类型+返回类型)
  │      ├─ Pass 3a: Scope Tree Construction — 每函数构建词法作用域树+符号表
  │      └─ Pass 3b: Shadow VM Run — 严格按作用域树遍历函数体(名称解析+类型检查+结果类型推导)
  │      │  语义错误：收集诊断（退出码 1）
  │
  ├─ ⑤ Bytecode Compiler ──→ 字节码模块（func/module 两粒度）
  │      │  AST 编译为线性字节码指令序列，类型已由 sema 验证，
  │      │  编译期计算（常量折叠）在表达式粒度编译→执行→嵌入。
  │      ├─ 函数级：函数体编译为字节码，延迟到运行时执行
  │      ├─ 表达式级：常量折叠、编译期计算（编译→执行→得到常量→嵌入上层）
  │      └─ 模块级：全局初始化代码编译为模块入口字节码
  │      │  编译错误：收集诊断（退出码 1）
  │
  ├─ ⑥ Bytecode VM ──→ 执行结果（退出码 0）
  │      栈式字节码执行器，PC 指针驱动，支持暂停/恢复。
  │      运行时不再做类型检查（已由 sema 完成），执行器更精简。
  │      运行时错误（如除零、空指针）→ 退出码 2
  │
  ▼
  诊断通道：所有阶段的诊断收进公共 diag 收集器，driver 统一打印到 stderr
```

错误与退出码契约：

| 错误类别 | 恢复策略 | 退出码 |
|----------|----------|--------|
| 词法错误（Lexer） | 产出 TOKEN_TYPE_ERROR 并继续；上层决定致命性 | 1 |
| 语法错误（Parser） | panic mode 跳到语句边界，可收集多条 | 1 |
| 语义错误（Sema） | 收集诊断，不编译不执行 | 1 |
| 运行时错误（Bytecode VM） | 立即终止 | 2 |

### 1.0 为什么需要 Driver

`cmd_run` 只负责参数解析，真正的流水线编排在独立 `driver` 模块（`include/driver/driver.h`, `src/driver/driver.c`）：

- `driver_compile_file()`：加载 + lex + parse + sema，产物是挂在 arena 上的 AST 与符号表
- `driver_run_file()`：compile + bytecode compile + execute，返回进程退出码
- 未来 `clux build`（转译）复用 compile + bytecode compile 段，`clux run` 复用全流程

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
| 3a   | Scope Tree Construction | 遍历每个函数体，构建词法作用域树（block 结构 → scope 树），每个 scope 节点注册完整符号表 |
| 3b   | Shadow VM Run | 按预建作用域树严格对应地遍历函数体，用 shadow value 执行类型检查与结果类型推导 |

三遍扫描的好处：
- 消除前置声明的需求，函数和类型定义顺序自由
- Pass 1 快速检测重复定义
- Pass 2 建立完整的类型信息，使 Pass 3 中的函数调用能正确匹配签名
- Pass 3 拆分为 3a/3b 两个子阶段：作用域结构与类型检查彻底分离。3a 只建结构（scope 树 + 符号表，不做类型检查），3b 按树遍历做类型检查（scope 管理与检查逻辑不纠缠）
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

**位置信息**：节点不嵌入 `location_t`（~56 字节），而是存储 token pool 下标（`tok_begin` / `tok_end`，共 8 字节）。诊断时通过 `vec_get(tokens, node->tok_begin)->location` 取得完整源码位置。

#### 2.4.1 公共头

```c
typedef struct ast_node {
    ast_kind_t       kind;        // 节点种类
    uint32_t         tok_begin;   // token pool 起始下标（inclusive）
    uint32_t         tok_end;     // token pool 结束下标（exclusive）
    struct ast_node *parent;      // 父节点（构建时填充）
    struct ast_node *next;        // 兄弟链（语句列表/参数/实参）
} ast_node_t;
```

- `tok_begin` / `tok_end` 是 Parser 持有的 token pool（`vec_t*`）中的下标
- 诊断时：`vec_get(tokens, node->tok_begin)` → `token_t*` → `token_get_location()` → `location_t`
- `uint32_t` 足够（单文件不可能超过 4G tokens）

#### 2.4.2 节点种类

```c
typedef enum {
    // --- 顶层 ---
    AST_PROGRAM,         // 函数定义列表
    AST_FUNC_DEF,        // func name(params):type { body }

    // --- 语句 ---
    AST_VAR_DEF,         // var name[:type] [= init];
    AST_ASSIGN,          // name = expr; / name += expr;
    AST_IF,              // if cond { then } [else { else_body }]
    AST_WHILE,           // while cond { body }
    AST_FOR,             // for (init; cond; update) { body }
    AST_RETURN,          // return [expr];
    AST_BREAK,           // break;
    AST_CONTINUE,        // continue;
    AST_BLOCK,           // { stmts... }
    AST_EXPR_STMT,       // expr;（表达式作为语句）
    /* AST_DISCARD removed: _ = expr is AST_ASSIGN, discard semantics in Sema */

    // --- 表达式 ---
    AST_BINARY,          // lhs op rhs
    AST_UNARY,           // op expr
    AST_CALL,            // name(args...)
    AST_INT_LIT,         // 整数字面量（原始文本切片，含后缀）
    AST_FLOAT_LIT,       // 浮点字面量（原始文本切片，含后缀）
    AST_BOOL_LIT,        // true / false
    AST_STRING_LIT,      // "..."（原始文本含引号，转义原样）
    AST_CHAR_LIT,        // 'a'（原始文本含引号，转义原样）
    AST_IDENT,           // 标识符引用
    AST_CAST,            // expr as type

    AST_ERROR,           // 解析错误恢复节点（记录错误位置，占位）

    AST_KIND_COUNT,      // 哨兵值，用于数组索引
} ast_kind_t;
```

#### 2.4.3 子类定义

**顶层节点**：

```c
typedef struct {
    ast_node_t  base;
    ast_node_t *funcs;       // 函数定义兄弟链首
    ast_node_t *funcs_last;  // O(1) 追加
} ast_program_t;

typedef struct {
    ast_node_t  base;
    strslice_t  name;        // 函数名
    ast_node_t *params;      // AST_VAR_DEF 兄弟链
    ast_node_t *params_last; // O(1) 追加
    strslice_t  return_type; // 返回类型文本（空切片 = void）
    ast_node_t *body;        // AST_BLOCK
} ast_func_def_t;
```

**语句节点**：

```c
typedef struct {
    ast_node_t  base;
    strslice_t  name;        // 变量名
    strslice_t  type_name;   // 类型标注（空切片 = 推断）
    ast_node_t *init;        // 初始化表达式（NULL = undefined/TDZ）
    bool        is_tdz;      // var x:i32 = undefined;
} ast_var_def_t;

typedef struct {
    ast_node_t  base;
    strslice_t  name;        // 赋值目标标识符
    int         op;          // '=' / '+=' / '-=' / '*=' / '/=' / '%='
    ast_node_t *value;       // 右值
} ast_assign_t;

typedef struct {
    ast_node_t  base;
    ast_node_t *cond;
    ast_node_t *then_body;   // AST_BLOCK
    ast_node_t *else_body;   // AST_BLOCK 或 NULL
} ast_if_t;

typedef struct {
    ast_node_t  base;
    ast_node_t *cond;
    ast_node_t *body;        // AST_BLOCK
} ast_while_t;

typedef struct {
    ast_node_t  base;
    ast_node_t *init;        // AST_VAR_DEF / AST_ASSIGN / NULL
    ast_node_t *cond;        // 表达式 / NULL
    ast_node_t *update;      // AST_ASSIGN / NULL
    ast_node_t *body;        // AST_BLOCK
} ast_for_t;

typedef struct {
    ast_node_t  base;
    ast_node_t *value;       // NULL = return;
} ast_return_t;

// AST_BREAK / AST_CONTINUE — 无额外字段

typedef struct {
    ast_node_t  base;
    ast_node_t *stmts;       // 语句兄弟链首
    ast_node_t *stmts_last;  // O(1) 追加
} ast_block_t;

typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
} ast_expr_stmt_t;
```

**表达式节点**：

```c
typedef struct {
    ast_node_t  base;
    int         op;          // 运算符（对应 token symbol 文本）
    ast_node_t *lhs;
    ast_node_t *rhs;
} ast_binary_t;

typedef struct {
    ast_node_t  base;
    int         op;          // '!' / '~' / '-'
    ast_node_t *operand;
} ast_unary_t;

typedef struct {
    ast_node_t  base;
    strslice_t  name;        // 函数名
    ast_node_t *args;        // 实参兄弟链
    ast_node_t *args_last;   // O(1) 追加
} ast_call_t;

typedef struct {
    ast_node_t  base;
    strslice_t  text;        // 原始切片（含后缀，如 "42u64"）
} ast_int_lit_t;

typedef struct {
    ast_node_t  base;
    strslice_t  text;        // 原始切片（含后缀，如 "3.14f32"）
} ast_float_lit_t;

typedef struct {
    ast_node_t  base;
    bool        value;
} ast_bool_lit_t;

typedef struct {
    ast_node_t  base;
    strslice_t  text;        // 含引号的原始切片
} ast_string_lit_t;

typedef struct {
    ast_node_t  base;
    strslice_t  text;        // 含引号的原始切片
} ast_char_lit_t;

typedef struct {
    ast_node_t  base;
    strslice_t  name;        // 标识符文本
} ast_ident_t;

typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
    strslice_t  target_type; // 目标类型文本
} ast_cast_t;

typedef struct {
    ast_node_t  base;
    strslice_t  message;     // 错误描述信息
    ast_node_t *node;        // 发生错误的子节点（可为 NULL）
} ast_error_t;
```

#### 2.4.4 工厂与辅助

```c
// 根据 kind 分配正确大小的子类，填充 kind + tok_begin/tok_end
ast_node_t *ast_new(arena_t *arena, ast_kind_t kind,
                    uint32_t tok_begin, uint32_t tok_end);

// 追加 node 到 *head / *last 兄弟链尾，设置 node->parent
void ast_append(ast_node_t **head, ast_node_t **last,
                ast_node_t *parent, ast_node_t *node);

// 查询
const char *ast_kind_name(ast_kind_t kind);
size_t      ast_kind_size(ast_kind_t kind);
```

#### 2.4.5 设计约束

1. **禁止** 在 AST 节点中嵌入 `location_t`——位置通过 token 下标间接访问
2. **禁止** 逐节点 free——AST 挂 arena，整体释放
3. **禁止** 在 Parser 阶段解码字面量值——保留原始 `strslice_t` 文本，解码是 Sema 的事
4. **禁止** 在 `ast_node_t` 中放 `last` 指针——尾指针属于容器节点（block/func_def/call/program）
5. 赋值节点 (AST_ASSIGN) 返回 void，不允许连续赋值，不能作为表达式嵌套
6. `AST_BREAK` / `AST_CONTINUE` 无子类结构，仅需 kind + tok range
7. `AST_ERROR` 是完整的错误报告节点，含 `message`（错误描述）和 `node`（发生错误的子节点范围），用于 panic mode 恢复时占位及后续诊断

### 2.5 VM 核心 (include/vm/*.h, src/vm/*.c) —— 已实现

VM 是 clux 的值计算引擎，贯穿语义分析和字节码执行两个阶段。M1 阶段 VM 的定位是**表达式求值器和作用域/变量生命周期管理工具**。

#### 2.5.1 类型系统 (include/vm/type.h, src/vm/type.c)

类型不是独立的 sema 子系统，而是 VM 的一部分。每个类型携带 vtable（虚表），类型行为通过 vtable 函数指针分派。

```c
typedef struct type {
    const char       *name;
    uint64_t          size;
    uint64_t          align;
    const vtable_t   *vtable;
} type_t;
```

基础类型单例：`type_i8()` … `type_i64()`, `type_u8()` … `type_u64()`, `type_f32()`, `type_f64()`, `type_bool()`, `type_str()`, `type_void()`。

每个类型通过 vtable 暴露行为：

```c
typedef struct vtable {
    /* 生命周期 */
    void    (*dispose)(vm_t *vm, value_t *v);
    value_t *(*clone)(vm_t *vm, value_t *v);
    value_t *(*assign)(vm_t *vm, value_t *dst, value_t *src);
    /* 类型转换 */
    value_t *(*implicit_cast)(vm_t *vm, value_t *v, const type_t *target);
    value_t *(*explicit_cast)(vm_t *vm, value_t *v, const type_t *target);
    /* 二元运算 */
    value_t *(*binary)(vm_t *vm, int op, value_t *lhs, value_t *rhs);
    /* 一元运算 */
    value_t *(*unary)(vm_t *vm, int op, value_t *v);
} vtable_t;
```

- `assign`：原地赋值，向左值类型 implicit_cast + memcpy，不涉及类型协商/promote
- `implicit_cast`：安全隐式转换（如 i8→i32 宽化）
- `explicit_cast`：显式转换（`as` 运算符），允许窄化
- `binary`：二元运算，内部经 promote → implicit_cast → safe_cast 协商后执行
- vtable 槽为 NULL 表示该类型不支持此操作，调用时返回 error

类型实现各自独立文件：`type_int.c`（signed/unsigned 共享 assign）、`type_float.c`、`type_bool.c`、`type_str.c`、`type_func.c`、`type_error.c`、`type_void.c`、`type_type.c`。

#### 2.5.2 值 (include/vm/value.h, src/vm/value.c)

`value_t` 为**不透明类型**，`struct value_t` 定义仅在 `value.c` 中，外部通过访问器操作：

```c
const type_t *value_type(const value_t *v);
void         *value_data(const value_t *v);
```

值使用 `void *data` 指向按 `type->size` 分配的堆内存（非 union），支持任意宽度类型。VM 全局持有所有 type 和 function 对象。

#### 2.5.3 作用域 (include/vm/scope.h, src/vm/scope.c)

统一所有权模型：`vars` 做名称→值的借用映射，`owned` 向量管理值生命周期。

- `scope_define(vm, scope, name, v)`：定义变量，重复定义返回 error
- `scope_lookup(scope, name)`：查找变量（遍历父链）
- `scope_push(vm, parent)` / `scope_pop(vm, scope)`：进入/退出作用域
- `value_clone` / `value_make` 自动 track 到 `vm->current_scope->owned`
- 退出作用域时统一释放 owned 值，禁止手动 dispose

#### 2.5.4 函数 (include/vm/function.h, src/vm/function.c)

`func_t` 为透明类型（后续需继承），支持 FFI 和用户函数：

```c
struct func_t {
    cfunc_t         cfunc;          /* C 函数指针（FFI） */
    scope_t        *closure_scope;
    scope_t        *root_scope;
    const type_t  **params;
    size_t          param_count;
    const type_t   *return_type;
    strslice_t      name;
    bool            is_variadic;    /* FFI 可变参数（如 printf） */
};
```

- `func_vcall`：函数调用，非 variadic 函数检查参数数量，variadic 函数允许 `argc > param_count`
- 短路运算符 `&&` / `||` 不走 vtable binary 分派，在调用层按 op token 做惰性求值

### 2.6 Shadow Value (include/vm/value.h) —— 已实现（2026-09-08）

语义分析阶段使用 shadow value 做类型检查与推导。Shadow value 复用 VM vtable 运算路径，不引入独立的 sema 类型系统。

**核心机制：**

```c
struct value_t {
    const type_t *type;
    void         *data;       /* shadow 时为 NULL */
    bool          is_shadow;  /* true = 只做类型计算，无实际数据 */
    /* ... */
};
```

- `is_shadow=true` 时 `data=NULL`，所有运算只进行类型计算不操作实际数据
- `value_make_shadow(vm, type)` 构造器：分配 value_t，设 type，data=NULL，is_shadow=true，auto-track 到当前 scope
- `value_is_shadow(v)` 访问器（value_t 为不透明类型，外部经访问器判断）
- shadow 标志放在 `value_t` 内部（非 type_t 层面）
- clone 槽显式实现（int/float/bool/str/type 各有 clone），value_clone 对 shadow 直接返回新 shadow；NULL clone 槽 = error

**传播规则：**

| 运算 | 结果 |
|------|------|
| shadow ⊕ normal | shadow（结果只有类型，无数据） |
| shadow ⊕ shadow | shadow |
| assign/cast 对 shadow | 只检查类型兼容性，不拷贝数据 |
| clone 对 shadow | 返回新的 shadow（不分配 data） |
| dispose 对 shadow | 跳过 data 释放（data==NULL） |

**传播位置：** 在 **vtable 函数内部**（非 DISPATCH 宏层）。Shadow value 需要经历完整的类型协商流程（VTABLE_BINARY 的 promote、implicit_cast、safe_cast），在类型检查/协商完成之后、实际读写 data 之前检查 `is_shadow`。如果是 shadow 则用结果类型构造 shadow 返回值，不分配/拷贝 data。类型协商错误（如 i32 + f64 禁止隐式）由 vtable 返回 error value，sema 捕获后转为诊断。

**执行流水线中的位置：**

```
源码 → AST → 语义分析（shadow value 类型检查/推导）→ 字节码编译 → 字节码执行
```

- 每次执行脚本都先走语义分析，不是可选的
- 语义分析用 shadow value 遍历 AST，验证类型合法性、推导结果类型、检查变量定义
- 全部通过后才编译为字节码执行
- 运行时不再做类型检查，字节码执行器更精简

### 2.7 字节码 IR —— 尚未实现

放弃 AST 直走解释，改为编译 AST 到线性字节码后执行。

**核心动机：**
- AST 直走解释无法做到暂停/恢复（如 generator、async/await、debugger 断点）
- 线性字节码是状态机，可以保存 PC 指针随时暂停恢复
- 编译期计算需要复用同一套执行引擎

**指令集（设计阶段）：**
- 算术运算、比较运算、跳转、调用、返回
- load/store（变量加载/存储）
- 类型转换（implicit_cast / explicit_cast 的编译期静态分派或运行时 vtable 分派）
- 常量加载（编译期计算结果直接嵌入）

**编译流程三粒度分级：**

| 粒度 | 编译时机 | 用途 |
|------|----------|------|
| 表达式级 | 编译过程中 | 常量折叠、类型推导等编译期计算 |
| 函数级 | 延迟到运行时 | 函数体编译为字节码 |
| 模块级 | 模块加载时 | 全局初始化、模块顶层代码 |

为什么需要分级：编译期计算意味着编译过程中需要执行部分字节码。表达式级编译 → 执行 → 得到常量结果 → 嵌入上层字节码。函数级编译 → 延迟到运行时执行。模块级编译 → 模块加载时执行全局初始化代码。

**与现有 VM 的关系：**
- 字节码执行时仍可通过 vtable 分派类型运算，或编译时静态分派（类型已由 sema 确定）
- shadow value 解决类型层面编译期计算，字节码解决值层面执行和运行时
- 两者互补：shadow value 先做（语义分析基础设施），字节码 IR 后做（大工程）

**执行器设计：**
- 栈式或寄存器式 VM，PC 指针驱动
- 支持保存/恢复 PC 指针（暂停/恢复的基础）
- 运行时不再做类型检查（已由 sema shadow value 完成）

### 2.8 诊断 (include/diag/diagnostic.h, src/diag/diagnostic.c) —— 尚未实现（diag/ 目录为空）

**公共模块**：Lexer、Parser、Sema、Bytecode Compiler、Bytecode VM 共用同一个收集器，driver 统一在出口打印，而不是边错边打。

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

### 2.9 语义分析 (include/sema/sema.h, symbol.h + src/sema/sema.c, stmt.c, symbol.c) —— 尚未实现

语义分析分两个阶段：**先构建 sema 作用域树**（scope 节点带完整符号表），**再按作用域树用 shadow value 遍历 AST** 做类型检查与推导。作用域结构与类型检查彻底分离。

**核心契约：** sema 是处理类型错误的最后一个阶段。sema 通过后，字节码编译器和 VM 可以假定一切类型正确，运行时不再做类型检查。vtable 运算返回的 error value 被 sema 捕获并转为诊断，不传播到下游。

**类型解析预留：** 所有类型解析收敛到独立入口 `resolve_type(sema, name)`，M1 内部为 `type_find(vm, name)` 查表。未来类型本身是编译期表达式（`[N]T` / `[]T` / `*T` / `<T1,T2>` / const 修饰），需要替换为类型表达式求值器（type_expr AST → type_t），该接口形态保证调用方不变。

#### 2.9.1 模块结构

```
include/sema/
  sema.h       — sema_t 上下文 + 公共 API (sema_create / sema_analyze / sema_destroy)
  symbol.h     — sema_symbol_t, sema_scope_t（sema 侧符号表）

src/sema/
  sema.c       — 上下文管理 + 三遍编排 + 表达式 walker（shadow value 求值）
  stmt.c       — 语句 walker
  symbol.c     — sema 侧符号表实现
```

sema 侧符号表独立于 VM scope：sema 需要追踪 TDZ 状态、激活状态等**编译期语义概念**，不属于运行时 VM scope 的职责。VM scope 仅作为 shadow value 的生命周期容器（每函数一个）。

#### 2.9.2 数据结构

```c
typedef enum {
    SEMA_SCOPE_GLOBAL,    /* 全局作用域（函数名） */
    SEMA_SCOPE_FUNCTION,  /* 函数作用域（参数 + 函数体顶层变量） */
    SEMA_SCOPE_BLOCK,     /* 块作用域 */
    SEMA_SCOPE_FOR,       /* for 作用域（init 变量） */
} sema_scope_kind_t;

typedef struct sema_symbol_t {
    const type_t *type;       /* 已解析类型；NULL = 待推断（shadow VM 阶段填充） */
    bool          is_tdz;     /* TDZ 中（未初始化，只可赋值不可读取） */
    bool          is_assigned; /* 已赋值（退出 TDZ 的依据） */
    bool          is_active;  /* shadow VM 到达定义点后激活（遮罩机制） */
    func_t       *func;       /* 函数符号：Pass 2 填充签名 */
    sema_scope_t *func_scope; /* 函数符号：Pass 3a 填充作用域树 */
} sema_symbol_t;

typedef struct sema_scope_t {
    struct sema_scope_t *parent;
    vec_t              *children;   /* sema_scope_t* 子作用域（按出现顺序） */
    strmap_t           *symbols;    /* name -> sema_symbol_t* */
    sema_scope_kind_t   kind;
} sema_scope_t;

typedef struct sema_t {
    vm_t         *vm;                 /* 复用 VM 类型注册表 + vtable + shadow value */
    diag_buf_t   *diag;               /* 诊断收集器 */
    sema_scope_t *global_scope;       /* 全局作用域树根 */

    /* 函数上下文（Pass 3b 时设置） */
    const type_t *func_return_type;   /* NULL = void */
    bool          func_has_return;

    /* 循环上下文 */
    int           loop_depth;         /* 0 = 不在循环中 */
} sema_t;
```

作用域树示例（`{ if(cond) {} else{} }`）：

```
block_scope
├── then_block_scope    (block1)
└── else_block_scope    (block2)
```

#### 2.9.3 Pass 1/2：Name / Type Collection

- **Pass 1**：遍历 `AST_PROGRAM` 顶层 `AST_FUNC_DEF` 兄弟链，函数名注册到 global scope，检测重复定义
- **Pass 2**：解析参数类型与返回类型（经 `resolve_type`），创建 `func_t` 并填入 `sema_symbol_t.func`。参数类型未知 → 诊断

#### 2.9.4 Pass 3a：作用域树构建

遍历函数体，按词法块结构建树。只注册符号（名字 + 声明类型 + TDZ 标志），**不做类型检查**。推断类型的变量 `type=NULL`，留给 Pass 3b 填充。

```c
static sema_scope_t *build_func_scope(sema_t *sema, ast_func_def_t *fn) {
    sema_scope_t *fscope = sema_scope_new(SEMA_SCOPE_FUNCTION, sema->global_scope);

    /* 注册参数（已初始化，待激活） */
    for (ast_node_t *p = fn->params; p; p = p->next) {
        ast_var_def_t *vd = (ast_var_def_t*)p;
        sema_scope_define(fscope, vd->name, &(sema_symbol_t){
            .type = resolve_type(sema, vd->type_name),
            .is_active = false,
        });
    }

    /* 函数体 block 直接用 fscope（不再嵌套一层） */
    build_block(sema, (ast_block_t*)fn->body, fscope);
    return fscope;
}
```

块内语句的 scope 构建规则——只在创建子作用域的节点上递归：

| AST 节点 | 作用域动作 |
|----------|-----------|
| `AST_VAR_DEF` | 注册符号到当前 scope（`is_tdz` 从节点标志拷贝；`type==NULL` 表示待推断） |
| `AST_BLOCK` | 新建子 scope，递归构建 |
| `AST_IF` | then_body / else_body 各建一个子 scope（else 为嵌套 if 时同层递归） |
| `AST_WHILE` | body 建一个子 scope |
| `AST_FOR` | 建 for scope（init 变量注册到 for scope），body 建 for scope 的子 scope |
| 其他语句 | 不创建作用域 |

#### 2.9.5 Pass 3b：Shadow VM 运行

按预建作用域树，严格对应地遍历 AST。核心机制：

**子作用域迭代器（child_idx）**：两个阶段遍历同一棵 AST，子作用域出现顺序一致。Shadow VM 用 `size_t child_idx` 按序取子作用域（`vec_get(scope->children, child_idx++)`），保证作用域严格对应。

**is_active 遮罩机制**：符号在定义点才激活。lookup 沿 parent 链查找，只返回 active 符号——正确处理变量名遮罩与"init 表达式引用外层同名变量"：

```
var x:i32 = 1;           // outer x
{
    var x = x + 1;       // init 的 x 引用 outer x（inner x 未 active）
                         // init 求值后才激活 inner x
    x = x + 2;           // 此处的 x 是 inner x（已 active，遮罩 outer）
}
x = x + 3;               // outer x
```

```c
static sema_symbol_t *sema_lookup(sema_scope_t *scope, strslice_t name) {
    for (sema_scope_t *s = scope; s; s = s->parent) {
        sema_symbol_t *sym = strmap_get(s->symbols, name);
        if (sym && sym->is_active) return sym;
    }
    return NULL;
}
```

**TDZ 变量处理**：`var x:i32 = undefined;` 注册时 `is_tdz=true, is_active=true`（立即可见），lookup 命中后检查 `is_tdz` 报告"未初始化读取"；赋值退出 TDZ（`is_tdz=false, is_assigned=true`）。

#### 2.9.6 表达式 walker（shadow value 求值）

每个表达式节点返回一个 **shadow value**（只有类型，data=NULL）。shadow value 经 vtable 运算路径，类型协商结果即为推导结果类型。

```c
static value_t *sema_expr(sema_t *sema, ast_node_t *node, sema_scope_t *scope);
```

| AST 节点 | 处理 |
|----------|------|
| `AST_INT_LIT` | 解析后缀定类型（`42i8`→i8，`42`→i32 默认） |
| `AST_FLOAT_LIT` | 解析后缀定类型（`3.14f32`→f32，`3.14`→f64 默认） |
| `AST_BOOL_LIT` | shadow bool |
| `AST_STRING_LIT` | shadow str |
| `AST_CHAR_LIT` | shadow u8 |
| `AST_IDENT` | `sema_lookup` 查符号 → shadow(类型)；未定义 / TDZ → 诊断 |
| `AST_BINARY` | lhs/rhs shadow 求值 → vtable 分派；短路 `&&`/`||` 特殊处理（操作数必须 bool，结果 bool） |
| `AST_UNARY` | 操作数 shadow → vtable 一元分派 |
| `AST_CALL` | `shadow_call`：校验签名 → 返回 return_type shadow |
| `AST_CAST` | `resolve_type` 目标 → `value_explicit_cast` 校验转换合法性 |

二元运算（非短路）走 vtable，类型不兼容时返回 error value，sema 转为诊断：

```c
static value_t *shadow_binary(sema_t *sema, ast_binary_t *node, sema_scope_t *scope) {
    if (is_short_circuit(node->op)) {
        value_t *lhs = sema_expr(sema, node->lhs, scope);
        check_bool(sema, node->lhs, lhs, "logical operator");
        value_t *rhs = sema_expr(sema, node->rhs, scope);
        check_bool(sema, node->rhs, rhs, "logical operator");
        return value_make_shadow(sema->vm, sema->vm->type_bool);
    }

    value_t *lhs = sema_expr(sema, node->lhs, scope);
    value_t *rhs = sema_expr(sema, node->rhs, scope);
    value_t *result = dispatch_binary(sema->vm, node->op, lhs, rhs);

    if (value_is_error(sema->vm, result)) {
        diag_error(sema->diag, loc(node), "type mismatch: %s %.*s %s", ...);
        return value_make_shadow(sema->vm, sema->vm->type_void);
    }
    return result;  /* shadow in → shadow out */
}
```

#### 2.9.7 函数调用（shadow 版本）

**value_call / func_vcall 完全不动**——保持运行时语义（cfunc 执行、作用域切换、返回值 clone）。sema 阶段用户函数 cfunc 为 NULL，`func_vcall` 会返回 "invalid function"，所以 shadow 版本走独立的参数校验路径，但**校验逻辑与 func_vcall 对齐**（数量检查 + 逐参数 `value_implicit_cast` 验证兼容性）。差异仅在调用之后：运行时走 cfunc 执行，shadow 版本校验完参数直接构造 `return_type` 的 shadow value。

```c
static value_t *shadow_call(sema_t *sema, ast_call_t *node, sema_scope_t *scope) {
    sema_symbol_t *sym = sema_lookup(sema->global_scope, node->name);
    if (!sym || !sym->func) {
        diag_error(sema->diag, loc(node), "undefined function '%.*s'", ...);
        return value_make_shadow(sema->vm, sema->vm->type_void);
    }
    func_t *fn = sym->func;

    /* 参数数量校验（与 func_vcall 一致：variadic 允许 argc > param_count） */
    size_t argc = count_siblings(node->args);
    if (!fn->is_variadic && argc != fn->param_count)
        diag_error(..., "expects %zu arguments, got %zu", fn->param_count, argc);
    else if (fn->is_variadic && argc < fn->param_count)
        diag_error(..., "variadic function expects at least %zu arguments", ...);

    /* 逐个参数：shadow 求值 + implicit_cast 校验（与 func_vcall 对齐） */
    ast_node_t *arg = node->args;
    for (size_t i = 0; arg; i++, arg = arg->next) {
        value_t *av = sema_expr(sema, arg, scope);
        if (i < fn->param_count && fn->params[i]
            && !type_eq(value_type(av), fn->params[i])) {
            value_t *casted = value_implicit_cast(sema->vm, av, fn->params[i]);
            if (value_is_error(sema->vm, casted))
                diag_error(..., "argument %zu: cannot convert %s to %s", ...);
        }
        /* variadic 额外参数：求值但类型不限（printf 的 ...） */
    }

    /* shadow 版本：不执行 cfunc、不做作用域切换，直接构造 return_type 的 shadow value */
    return value_make_shadow(sema->vm,
        fn->return_type ? fn->return_type : sema->vm->type_void);
}
```

#### 2.9.8 语句 walker 与控制流分析

语句不返回值，但有副作用（定义变量、检查赋值规则、验证控制流）。每个语句返回 `block_result_t{bool definitely_returns}` 用于返回路径完整性分析。

```c
static block_result_t sema_stmt(sema_t *sema, ast_node_t *stmt,
                                 sema_scope_t *scope, size_t *child_idx);
```

| 语句 | 处理要点 |
|------|---------|
| `AST_VAR_DEF` | 非 TDZ：先求值 init（符号未激活 → 自引用解析到外层），再推断/校验类型，最后激活符号。TDZ：`is_tdz=true, is_active=true` |
| `AST_ASSIGN` | `_ = expr` 为显式丢弃；简单赋值检查 `type_assignable` + 退出 TDZ；复合赋值 `x op= rhs` 展开为 `x = x op rhs`（shadow 走 vtable 协商）；TDZ 变量只允许简单赋值 |
| `AST_BLOCK` | 取子 scope（`child_idx++`），递归遍历 |
| `AST_IF` | 条件必须 bool；then/else 各取子 scope；`definitely_returns = then && else` |
| `AST_WHILE` | 条件必须 bool；`loop_depth++` 后遍历 body；不贡献 definitely_returns（循环体可能不执行） |
| `AST_FOR` | init 在 for scope；条件必须 bool；body 是 for scope 的子 scope；`loop_depth++` 遍历 |
| `AST_RETURN` | 校验值类型可赋给返回类型；void 函数禁止返回值；返回 `definitely_returns=true` |
| `AST_BREAK/CONTINUE` | `loop_depth == 0` → "break/continue outside loop" 诊断 |
| `AST_EXPR_STMT` | 结果必须为 void，否则必须用 `_ = ...` 显式丢弃 |
| `AST_BLOCK`（顶层） | `definitely_returns` 传递到函数级：非 void 函数所有路径必须 return |

#### 2.9.9 类型解析与赋值兼容性

```c
/* 类型解析唯一入口：M1 内部 type_find；未来替换为类型表达式求值器 */
const type_t *resolve_type(sema_t *sema, strslice_t name);

/* 赋值兼容性：dst 可接受 src 当且仅当 implicit_cast 成功 */
static bool type_assignable(sema_t *sema, const type_t *dst, const type_t *src) {
    if (type_eq(dst, src)) return true;
    value_t *s = value_make_shadow(sema->vm, src);
    value_t *c = value_implicit_cast(sema->vm, s, dst);
    return !value_is_error(sema->vm, c);
}
```

M1 无 const/volatile（不在 M1 阶段实现），符号表与类型检查不含 const 规则。

#### 2.9.10 与 VM 的集成

| VM 设施 | sema 中的用途 |
|---------|-------------|
| `type_find(vm, name)` | `resolve_type` 内部实现 |
| `value_make_shadow(vm, type)` | 为每个表达式构造类型标记 |
| `value_add/sub/mul/...` | 二元运算类型推导（shadow 输入 → shadow 输出） |
| `value_implicit_cast` | 赋值/函数参数兼容性检查 |
| `value_explicit_cast` | as 转换合法性检查 |
| `vm_push/pop_scope` | 每函数一个，shadow value 生命周期容器 |
| `func_t`（Pass 2 创建） | 函数签名来源（shadow_call 校验用） |

**不使用的 VM 设施**：`value_call` / `func_vcall`（sema 不执行函数调用，shadow_call 独立校验签名）。

#### 2.9.11 公共 API 与 driver 集成

```c
sema_t *sema_create(vm_t *vm, diag_buf_t *diag);
bool    sema_analyze(sema_t *sema, ast_node_t *program);  /* 三遍扫描 */
void    sema_destroy(sema_t **sema);
```

```c
if (parser_error(parser)) return 1;          /* 语法错误不进 sema */

sema_t *sema = sema_create(vm, diag);
if (!sema_analyze(sema, ast)) {
    diag_print_all(diag);                    /* 语义错误 */
    return 1;
}
/* sema 通过 → 进入字节码编译 */
```

作用域树是持久化数据，sema 结束后不销毁，交由字节码编译器复用（变量类型静态分派、作用域结构定位 load/store、TDZ 初始化信息）。

### 2.10 Driver（include/driver/driver.h, src/driver/driver.c）—— 已实现（当前阶段：加载 + 词法 → 单词表）

流水线编排者，见本文档第 1 节。当前落地阶段 ①（加载源码）与 ②（词法分析），并直接完成 ③（输出单词表）：

```c
// 加载源码到内存缓冲（allocator 管理，data 在 allocator 存活期间有效）。
int driver_load_source(allocator_t *alloc,
                       const char *path,
                       const char **out_data,
                       size_t     *out_len);

// 加载 + 词法分析 -> token 池（vec<token_t*>, owns_element=true）。
// 返回 0 成功 / -1 文件无法打开；词法错误以 TOKEN_TYPE_ERROR 留在池中。
int driver_lex_file(allocator_t *alloc, const char *path, vec_t **out_pool);

// 顶层入口：加载 -> 词法 -> 输出单词表。
// 返回退出码：0 成功，1 编译错误（文件打不开 / 词法错误）。
int driver_run_file(const char *path);
```

`cmd_run`（src/cmd/run.c）只做参数解析（取首个位置参数作为文件路径），其余编排下沉到 `driver_run_file`。

**单词表输出格式（阶段 ③）**：每个 token 独占一行（EOF 作为终止符跳过不打印），打印其源码位置范围与转义后的文本：

```
<TOKEN_KIND> L<begin.line>:<begin.col>-<end.line>:<end.col> <escaped-text>
```

示例：`KEYWORD L1:1-1:5 func`；`STRING L2:10-2:15 \"ab\n\"`。控制字符转义为字面 `\n`/`\r`/`\t`/`\\`/`\"`，其他不可打印字节转 `\xHH`，**不输出真实换行/制表符**。词法错误行额外追加 ` error: <message>`，并同时向 stderr 打印诊断 `<file>:<line>:<col>: error: <message>`，退出码置 1。

**内存源约束**：Lexer 要求内存直读源（`istream_data != NULL`），而 `stream_source_file` 的 `data()` 为 NULL，因此阶段 ① 先把文件读入 allocator 缓冲，再用 `stream_source_mem(allocator, buf, len, owns_data=true)` 建内存源——缓冲由 istream/lexer 生命周期自动释放。token 文本切片在 lexer 存活期间有效，故单词表在 `lexer_close` 之前打印完毕。

`driver_compile_file` / `driver_run_file` 的完整签名（含 parse/sema/bytecode compile/execute）待后续阶段接入 Parser/Sema/Bytecode 后补全。Driver 持有的编译单元 arena 使所有阶段产物（AST、符号表、token 文本指向的源 buffer）在同一生命周期内有效。

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
| `clux_vm` | `src/vm/*.c` | 值计算引擎：type/vtable/value/scope/function（已实现），依赖 `clux_core` + `clux_parser` + ICU |
| `clux_driver` | `src/driver/*.c` | 流水线编排（加载 + 词法，后续加 parse/sema/bytecode），依赖 `clux_core` + `clux_parser` + `clux_vm` |
| `clux_sema` | `src/sema/*.c` | 语义分析（shadow value 驱动）；**目录为空时不创建目标**（GLOB + `if`） |
| `clux_diag` | `src/diag/*.c` | 诊断收集器；同上 |
| `clux_cmd` | `src/cmd/*.c` | 子命令分发，依赖 `clux_driver` |

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

- **单元测试**：Lexer、Parser、VM（type/vtable/value/scope/function）各模块独立测试，当前 597 测试通过
- **集成测试**：.cx 程序端到端执行，对比输出
- **测试用例**：hello.cx、arithmetic.cx、functions.cx、control_flow.cx、fibonacci.cx
