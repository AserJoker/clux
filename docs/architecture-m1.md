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
  │      │  使用 shadow value（is_shadow=true, data=NULL）遍历 AST，
  │      │  复用 VM vtable 类型协商路径做类型检查与推导。
  │      │  所有类型验证通过后才进入字节码编译。
  │      ├─ Pass 1: Name Collection — 收集所有顶层名称(函数名)
  │      ├─ Pass 2: Type Collection — 收集函数签名(参数类型+返回类型)
  │      └─ Pass 3: Body Processing  — shadow value 遍历函数体(名称解析+类型检查+结果类型推导)
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

### 2.6 Shadow Value (include/vm/value.h) —— 尚未实现

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
- `value_make_shadow(vm, type)` 构造器：分配 value_t，设 type，data=NULL，is_shadow=true
- shadow 标志放在 `value_t` 内部（非 type_t 层面）

**传播规则：**

| 运算 | 结果 |
|------|------|
| shadow ⊕ normal | shadow（结果只有类型，无数据） |
| shadow ⊕ shadow | shadow |
| assign/cast 对 shadow | 只检查类型兼容性，不拷贝数据 |
| clone 对 shadow | 返回新的 shadow（不分配 data） |

**传播位置：** 在 **vtable 函数内部**（非 DISPATCH 宏层）。Shadow value 需要经历完整的类型协商流程（VTABLE_BINARY 的 promote、implicit_cast、safe_cast），在类型检查/协商完成之后、实际读写 data 之前检查 `is_shadow`。如果是 shadow 则用结果类型构造 shadow 返回值，不分配/拷贝 data。

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

### 2.9 语义分析 (include/sema/resolver.h, src/sema/resolver.c) —— 尚未实现

遍历 AST，使用 shadow value 做类型检查与推导：
1. **名称解析**：绑定标识符到声明，检查变量定义
2. **类型检查**：用 shadow value 遍历表达式，复用 vtable 类型协商路径验证类型合法性
3. **类型推导**：shadow value 运算结果携带推导出的类型信息
4. 全部通过后才进入字节码编译，运行时不再做类型检查

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
