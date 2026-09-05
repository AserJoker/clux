# clux 编译器架构 — M1

## 1. Pipeline

```
.cx 源文件
    │
    ▼
  Lexer ──→ Token 流
    │
    ▼
  Parser ──→ AST
    │
    ▼
  Semantic Analysis (顶层多遍扫描)
  ├─ Pass 1: Name Collection — 收集所有顶层名称(函数名)
  ├─ Pass 2: Type Collection — 收集函数签名(参数类型+返回类型)
  └─ Pass 3: Body Processing  — 处理函数体(名称解析+类型检查)
    │
    ▼
  Interpreter (AST-walking)
    │
    ▼
  执行结果
```

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

### 2.1 Lexer (include/clux/lexer.h, src/clux/lexer.c)

输入源文件字符流，输出 Token 流。

- `lexer_create(source_t)` → `lexer_t*`
- `lexer_next(lexer_t*)` → `token_t`
- 支持所有 M1 关键字、运算符、字面量
- 跳过注释和空白
- 每个 Token 携带源码位置（文件/行/列）

### 2.2 Token (include/clux/token.h, src/clux/token.c)

```c
typedef enum {
    TK_EOF, TK_IDENT, TK_INT_LIT, TK_FLOAT_LIT, TK_BOOL_LIT,
    TK_KEYWORD, TK_PUNCT,
} token_kind_t;

typedef struct {
    token_kind_t kind;
    location_t   loc;
} token_t;
```

### 2.3 Parser (include/clux/parser.h, src/clux/parser.c)

递归下降解析器，Token 流 → AST。

- `parser_create(lexer_t*)` → `parser_t*`
- `parser_parse(parser_t*)` → `ast_node_t*`（程序根节点）
- 表达式解析用 Pratt parsing
- 错误恢复：panic mode（跳到下一个语句边界）

### 2.4 AST (include/clux/ast.h, src/clux/ast.c)

M1 节点类型：

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
    AST_IDENT,
    AST_CAST,         // <expr> as <type>
} ast_kind_t;
```

节点统一结构：

```c
typedef struct ast_node {
    ast_kind_t  kind;
    location_t  loc;
    type_t      resolved_type;  // 语义分析后填充
    union {
        // 按 kind 区分的具体数据
    } data;
} ast_node_t;
```

变量声明节点 (AST_VAR_DEF) 细节：

```c
typedef struct {
    char       *name;
    type_t      type;           // 显式标注的类型（可能为 NULL 表示推断）
    ast_node_t *init;           // 初始化表达式（NULL 表示 undefined/TDZ）
    bool        is_tdz;         // 是否处于 TDZ（var x:i32 = undefined;）
} ast_var_def_t;
```

赋值节点 (AST_ASSIGN) 说明：

- 赋值表达式返回 void，因此不允许连续赋值 `a = b = 1;`
- 只能作为语句使用，不能作为表达式嵌套

### 2.5 类型系统 (include/sema/type.h, src/clux/type.c)

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

### 2.6 鸭子类型兼容性 (include/sema/type_compat.h, src/sema/type_compat.c)

M1 实现：

```c
// 两个类型布局兼容
bool type_layout_compatible(type_t a, type_t b);

// 赋值兼容（布局兼容 + 安全隐式转换）
bool type_assign_compatible(type_t target, type_t source);
```

M1 仅处理原始类型：kind 相同即布局兼容。

### 2.7 作用域 (include/sema/scope.h, src/sema/scope.c)

```c
typedef struct scope {
    struct scope *parent;
    strmap_t      names;  // name -> symbol_t
} scope_t;
```

操作：`scope_push()`, `scope_pop()`, `scope_declare()`, `scope_lookup()`。

### 2.8 语义分析 (include/sema/resolver.h, src/sema/resolver.c)

遍历 AST：
1. 名称解析：绑定标识符到声明
2. 类型检查：表达式类型推断、赋值兼容性、函数调用参数匹配
3. 结果写入 AST 节点的 `resolved_type` 字段

### 2.9 诊断 (include/sema/diagnostic.h, src/sema/diagnostic.c)

```c
typedef enum { DIAG_ERROR, DIAG_WARNING, DIAG_NOTE } diag_level_t;

typedef struct {
    diag_level_t level;
    location_t   loc;
    char        *message;
} diagnostic_t;
```

收集并格式化输出错误信息。

### 2.10 解释器 (include/runtime/interp.h, src/runtime/interp.c)

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

## 3. 命令行接口

```
clux run <file.cx>    解释执行 .cx 文件
```

退出码：0 成功，1 编译错误，2 运行时错误。

## 4. 构建系统

CMake + 备选 Makefile。C11 标准，编译选项 `-Wall -Wextra -Wpedantic`。

## 5. 测试策略

- **单元测试**：Lexer、Parser、类型系统各模块独立测试
- **集成测试**：.cx 程序端到端执行，对比输出
- **测试用例**：hello.cx、arithmetic.cx、functions.cx、control_flow.cx、fibonacci.cx
