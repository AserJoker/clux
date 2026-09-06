# clux AST: 数据结构与内存模型

> 本文档是 `src/parser/ast.c` 与 `include/parser/ast.h` 的代码级约束文档。
> 后续开发者在新增/修改 AST 节点前必须阅读并遵守本文档中的规则。

---

## 1. 结构范式：公共头 + 子类化

所有节点共享公共头 `ast_node_t`，具体节点通过 `kind` 区分、按子类大小分配。**不使用**单 union 方案（避免每个节点膨胀到最大成员大小，且 M2+ 扩展节点不破坏现有代码）。

```c
typedef struct ast_node {
    ast_kind_t       kind;
    location_t       loc;
    type_t           resolved_type;  /* sema 填充，parser 不写 */
    struct ast_node *next;           /* 兄弟链表：语句列表 / 参数 / 实参 */
} ast_node_t;
```

访问子类字段：把 `ast_node_t*` 转型为对应子类指针。子类**必须**以 `ast_node_t base;` 作为首字段，保证转型安全。

## 2. 内存模型

- AST 整体挂在**编译单元 arena**（`allocator_t`）上，随 arena 释放，**不做逐节点 free**。
- 标识符/字符串/字符字面量一律存 `strslice_t`（零拷贝切片，指向源 buffer）：

```c
typedef struct { const char *ptr; size_t len; } strslice_t;
```

- 节点分配统一走工厂，禁止在模块外直接 `allocator_new` 分配节点：

```c
ast_node_t *ast_new(allocator_t *a, ast_kind_t kind, location_t loc);
```

工厂按 kind 查表得到子类大小 → 零初始化 → 填公共头。

## 3. 节点清单（M1 全集）

### 3.1 顶层

```c
/* AST_PROGRAM */
typedef struct {
    ast_node_t  base;
    ast_node_t *funcs;          /* AST_FUNC_DEF 链表 */
} ast_program_t;

/* AST_FUNC_DEF */
typedef struct {
    ast_node_t  base;
    strslice_t  name;
    ast_node_t *params;         /* ast_param_t 链表 */
    type_t      return_type;    /* 缺省 void */
    ast_node_t *body;           /* AST_BLOCK */
} ast_func_def_t;

/* 参数声明（无对应 ast_kind，作为 FUNC_DEF 的附属节点） */
typedef struct {
    ast_node_t  base;
    strslice_t  name;
    type_t     *annot;          /* M1 参数必须显式标注，恒非 NULL */
} ast_param_t;
```

### 3.2 语句

```c
/* AST_VAR_DEF：var name[:type] = init; */
typedef struct {
    ast_node_t  base;
    strslice_t  name;
    type_t     *annot;          /* NULL → 类型推断 */
    ast_node_t *init;           /* NULL → undefined/TDZ */
    bool        is_tdz;
} ast_var_def_t;

/* AST_ASSIGN：name op= expr；op 指向源文本切片（"=" " +=" 等） */
typedef struct {
    ast_node_t  base;
    strslice_t  name;           /* 赋值目标恒为标识符 */
    const char *op;             /* 运算符文本切片 */
    ast_node_t *rhs;
} ast_assign_t;

/* AST_IF */
typedef struct {
    ast_node_t  base;
    ast_node_t *cond;
    ast_node_t *then_body;      /* AST_BLOCK */
    ast_node_t *else_body;      /* NULL 表示无 else */
} ast_if_t;

/* AST_WHILE */
typedef struct {
    ast_node_t  base;
    ast_node_t *cond;
    ast_node_t *body;           /* AST_BLOCK */
} ast_while_t;

/* AST_FOR：for (init; cond; update) body */
typedef struct {
    ast_node_t  base;
    ast_node_t *init;           /* AST_VAR_DEF 或 AST_ASSIGN，NULL 允许 */
    ast_node_t *cond;           /* NULL 允许（无限循环）？—— M1 不允许，恒非 NULL */
    ast_node_t *update;         /* AST_ASSIGN 或 AST_EXPR_STMT，NULL 允许 */
    ast_node_t *body;           /* AST_BLOCK */
} ast_for_t;

/* AST_RETURN */
typedef struct {
    ast_node_t  base;
    ast_node_t *value;          /* NULL → 裸 return */
} ast_return_t;

/* AST_BREAK / AST_CONTINUE：无数据字段，kind 即全部信息 */

/* AST_BLOCK */
typedef struct {
    ast_node_t  base;
    ast_node_t *stmts;          /* 语句链表 */
} ast_block_t;

/* AST_EXPR_STMT：表达式语句，M1 仅允许函数调用表达式 */
typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
} ast_expr_stmt_t;

/* AST_DISCARD：_ = expr; */
typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
} ast_discard_t;
```

### 3.3 表达式

```c
/* AST_BINARY */
typedef struct {
    ast_node_t  base;
    const char *op;             /* 运算符文本切片 */
    ast_node_t *lhs, *rhs;
} ast_binary_t;

/* AST_UNARY：op 为 "!" "~" "-" */
typedef struct {
    ast_node_t  base;
    const char *op;
    ast_node_t *operand;
} ast_unary_t;

/* AST_CALL：callee 恒为标识符（M1 无函数指针） */
typedef struct {
    ast_node_t  base;
    strslice_t  callee;
    ast_node_t *args;           /* 实参表达式链表 */
} ast_call_t;

/* AST_INT_LIT：值与类型在 parser 阶段解析完成（文本→值） */
typedef struct {
    ast_node_t  base;
    uint64_t    value;
    type_t      type;           /* 由后缀/推断决定（i32/i64/u8/...） */
} ast_int_lit_t;

/* AST_FLOAT_LIT */
typedef struct {
    ast_node_t  base;
    double      value;
    type_t      type;           /* 缺省 f64 */
} ast_float_lit_t;

/* AST_BOOL_LIT */
typedef struct {
    ast_node_t  base;
    bool        value;
} ast_bool_lit_t;

/* AST_STRING_LIT：str 类型；运行时无内存所有权 */
typedef struct {
    ast_node_t  base;
    strslice_t  value;          /* 已解码的内容（不含引号与转义） */
} ast_string_lit_t;

/* AST_CHAR_LIT：值类型 u8 */
typedef struct {
    ast_node_t  base;
    uint8_t     value;
} ast_char_lit_t;

/* AST_IDENT */
typedef struct {
    ast_node_t  base;
    strslice_t  name;
} ast_ident_t;

/* AST_CAST：expr as type */
typedef struct {
    ast_node_t  base;
    ast_node_t *expr;
    type_t      target;
} ast_cast_t;
```

## 4. 字面量解析（literal 解析）

Lexer 只产出原始文本切片（`"ab\n"` 含引号、`0xFFu8` 含后缀）。Parser 构建字面量节点时执行：

- **数字**：识别进制前缀 → 解析整数/浮点值 → 识别类型后缀决定 `type`。溢出与值域检查在此进行（诊断错误）。
- **字符**：剥离引号 → 解码转义序列 → 校验恰好一个字符（空 `''`、多字符 `'ab'` 为错误）→ 值域必须 ≤ 255。
- **字符串**：剥离引号 → 解码转义 → 得解码后的 strslice。

建议拆独立 `src/parser/literal.c`（数字/转义解码），便于单测。

## 5. 禁止事项

1. **禁止** 在 `ast.h` 之外手动拼装节点结构体——一律走 `ast_new`。
2. **禁止** 在 parser 阶段写 `resolved_type`（sema 独占）。
3. **禁止** 对字面量节点存"原始文本指针"而不解码——值必须在 AST 构建时确定。
4. **禁止** 用 `char*` 存标识符名（源文本非 NUL 结尾）——必须 `strslice_t`。
5. **禁止** 逐节点 free AST（arena 统一释放）。
6. **禁止** 增加 M1 之外的节点种类而不更新本文档与 `ast_kind_t`。
