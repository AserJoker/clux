# clux parser: 递归下降范式

> 本文档是 `src/parser/parser.c` 与 `include/parser/parser.h` 的代码级约束文档。
> 后续开发者在修改解析逻辑或新增语法前必须阅读并遵守本文档中的规则。

---

## 1. 总体范式

- **语句层：递归下降**，按当前 token 分派（switch）。
- **表达式层：Pratt parsing**，绑定力表驱动——不按优先级写 13 层嵌套函数。
- **前瞻**：lookahead=1（`parser_t::cur` 借用 `lexer_peek`）。M1 语法无歧义点需要深回溯；lexer 的 checkpoint/rewind 保留但 parser 不依赖。
- **错误恢复**：语法错误 panic mode（同步点 `;` `}` EOF）；词法错误 fail-fast（fatal，立即终止）。

## 2. parser_t 状态

```c
typedef struct parser {
    allocator_t   *alloc;
    lexer_t       *lexer;
    const token_t *cur;          /* 借用自 lexer_peek，advance 后失效 */
    diag_buf_t    *diags;        /* 公共诊断收集器 */
    bool           fatal;        /* 词法错误：立即终止解析 */
    int            loop_depth;   /* break/continue 合法性 */
    int            func_depth;
} parser_t;
```

生命周期：parser 不拥有 lexer（调用方负责 close）、不拥有 diags（调用方负责释放）。parser 只使用 arena 分配 AST。

## 3. Token 流管理

Lexer 产出所有 token（含空白/注释）。Parser 包一层 **trivia 跳过**，任何时刻内存只活一个 token（O(1)）：

```c
static void parser_advance(parser_t *p) {
    token_t *t;
    for (;;) {
        t = lexer_next(p->lexer);            /* 所有权转移 */
        token_free(p->alloc, &t);            /* 立即释放 */
        token_kind_t k = token_get_kind(lexer_peek(p->lexer));
        if (k != TOKEN_TYPE_WHITESPACE && k != TOKEN_TYPE_COMMENT
            && k != TOKEN_TYPE_MULTILINE_COMMENT) break;
    }
    p->cur = lexer_peek(p->lexer);
}
```

**词法错误检测**：`parser_advance` / 初始化 peek 时若 `token_get_kind == TOKEN_TYPE_ERROR` → 读 `lexer_error` 记 fatal 诊断 → `p->fatal = true` → 停止拉取。顶层 `parser_parse` 检查 `p->fatal` 立即返回。

原语（全部内部 static）：

| 原语 | 行为 |
|------|------|
| `parser_at(p, kind)` | 当前 token 是否某种类 |
| `parser_at_text(p, "func")` | 当前 token 文本是否等于给定串 |
| `parser_match(p, "=")` | 匹配则消费（advance）返回 true，否则 false |
| `parser_expect(p, ";")` | 不匹配 → 记录诊断 + panic recovery |
| `parser_peek_kind(p)` | 当前 kind |
| `parser_token_text(p)` | 当前 token 文本切片（`strslice_t`） |

## 4. 顶层与函数定义

```
parse_program  = while (!at(EOF)) func_def;   // 顶层只允许函数定义
parse_func_def = "func" IDENT "(" params ")" [":" type] block
params         = [param ("," param)*]
param          = IDENT ":" type               // M1 参数必须显式标注
```

- 返回类型缺省 `void`。
- 顶层出现非 `func` 开头的 token → 语法错误 + panic recovery。

## 5. 语句分派

```
parse_stmt（按 cur 分派）:
  "var"        → parse_var_def
  "if"         → parse_if
  "while"      → parse_while
  "for"        → parse_for
  "return"     → parse_return
  "break"      → parse_break     // loop_depth==0 → 诊断
  "continue"   → parse_continue  // loop_depth==0 → 诊断
  "{"          → parse_block
  "_"          → 后必跟 "=" → parse_discard（否则诊断）
  IDENT        → 前瞻下一个非 trivia token：
                   "=" / "+=" / "-=" / "*=" / "/=" / "%=" → parse_assign
                   否则 → parse_expr_stmt（仅允许 AST_CALL，否则诊断）
  其他         → 诊断 "expected statement"
```

要点：
- **赋值不是表达式**：`=` 系列不在 Pratt 绑定力表，`a = b = 1` 在 parse 层即死（`parse_assign` 的 rhs 走 `parse_expr(0)`，遇 `=` 停止，随后 `expect(";")` 失败）。
- **for 的 init**：`var` 声明或赋值语句，作用域覆盖整个 for（sema 负责作用域）。
- **循环深度**：`parse_for`/`parse_while` 进入时 `loop_depth++`，退出时 `--`；`break`/`continue` 在 `loop_depth == 0` 时诊断。
- **return 类型**：不在此检查（sema 负责匹配函数返回类型）。

## 6. 表达式：Pratt parsing

### 6.1 绑定力表（M1 13 级，直接映射语言规格优先级）

```c
static const struct { const char *op; int lbp; } k_infix[] = {
    {"||", 1}, {"&&", 2}, {"|", 3}, {"^", 4}, {"&", 5},
    {"==", 6}, {"!=", 6},
    {"<", 7}, {">", 7}, {"<=", 7}, {">=", 7},
    {"<<", 8}, {">>", 8},
    {"+", 9}, {"-", 9},
    {"*", 10}, {"/", 10}, {"%", 10},
    {"as", 11},          // 左结合中缀 cast
};
```

非中缀 token 的 lbp = 0（终止循环）。

### 6.2 核心循环

```c
static ast_node_t *parse_expr(parser_t *p, int min_bp) {
    ast_node_t *lhs = parse_prefix(p);
    for (;;) {
        int lbp = binding_power(p);
        if (lbp <= min_bp) break;
        const char *op = parser_token_text(p).ptr;
        parser_advance(p);
        ast_node_t *rhs = parse_expr(p, lbp);   /* 左结合 */
        lhs = ast_binary(p, op, lhs, rhs);
    }
    return lhs;
}
```

- **左结合**：右操作数以 `lbp` 为门槛（同优先级右侧不再吸收同级运算符）。
- **右结合**：若未来引入右结合运算符，右操作数传 `lbp - 1`。M1 中右结合只出现在前缀一元（见下）。
- **`as` 左结合**：`x as i32 as u8` = `(x as i32) as u8`，由 `parse_expr(11)` 保证。

### 6.3 parse_prefix（一元 / 字面量 / 标识符 / 调用）

```c
static ast_node_t *parse_prefix(parser_t *p) {
    switch (kind) {
      case "!": case "~": case "-":      // 一元，绑定力 12，右结合
          op = text; advance;
          return ast_unary(op, parse_expr(p, 12));
      case NUMERIC:  → 解析数字字面量（进制/后缀/值）→ INT_LIT / FLOAT_LIT
      case STRING:   → 解码 → STRING_LIT
      case CHARACTER:→ 解码 + 单字符校验 → CHAR_LIT
      case "true"/"false" → BOOL_LIT
      case IDENT:    → advance；若下一个非 trivia token 是 "(" → parse_call
                      否则 → IDENT 节点
      default:       → 诊断 "expected expression"
    }
}
```

`parse_call`：`"(" args ")"`，args 为空或 `expr ("," expr)*`（每个实参 `parse_expr(0)`）。

### 6.4 优先级处理的总原则

- 改优先级 = 改 `k_infix` 表，不改解析函数结构。
- `()` 调用后缀绑定力最高（13），实参解析用 `parse_expr(0)` 吸收完整表达式。
- 一元前缀绑定力 12：`-x * y` = `(-x) * y`；`- -x` 合法（`parse_expr(12)` 内再次 parse_prefix 吸收 `-`）。

## 7. panic mode 错误恢复

- 触发点：`parser_expect` 失败、表达式/语句无法开始。
- 同步点（停止跳过）：`;`、`}`、EOF。
- 遇到 `}` 时**消费它**（交给上层 block 收尾），避免上层 block 循环死等。
- 恢复后必须保证状态可继续：block 循环 `while (!at("}") && !at(EOF))`。
- 恢复期间遇到 `TOKEN_TYPE_ERROR` → 转为 fatal（词法错误优先）。

## 8. 与 sema 的边界

Parser 只负责：
- 构建 AST（值/转义在节点构建时解析）
- 纯语法检查：语句结构、break/continue 循环上下文、赋值语句目标为标识符、表达式语句必须为调用

以下**全部留给 sema**（Parser 不检查）：
- 名称解析、重复定义、未定义函数/变量
- 类型推断与检查、const 规则、TDZ 赋值规则
- 函数调用参数个数与类型匹配、返回值类型匹配
- `resolved_type` 字段填充

## 9. 测试策略

- 合法程序：验证 AST 形状（遍历 kind 序列）与关键字段。
- 非法程序：验证诊断条数与位置、恢复后继续解析、fatal 语义（词法错误立即终止，不产生后续诊断）。
- 优先级：`1 + 2 * 3`、`a as i32 as u8`、`-x * y`、`!a && b` 的 AST 形状。
- 回归：`a = b = 1` 必须报错；`_ = f()` 合法；`f();` 合法；`x;` 报错。

## 10. 禁止事项

1. **禁止** 用递归下降函数模拟优先级层级（每级一个 parse 函数）——必须走 Pratt 表。
2. **禁止** 把 `=` 系列加入绑定力表（赋值不是表达式）。
3. **禁止** 在 parser 中做类型检查或写 `resolved_type`。
4. **禁止** 对词法错误执行 panic recovery（fatal 立即终止）。
5. **禁止** 持有 `p->cur` 指针越过一次 `parser_advance`（借用失效）。
6. **禁止** 在 parser 内 free 已构建的 AST 节点（arena 统一释放）。
