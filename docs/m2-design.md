# clux M2 设计文档

## 目标

**表达力与 C 齐平（指针除外）**，非照搬 C 语法。clux 保持自己的语法风格（`as` 而非 `(type)`、`var` 推断、严格 bool、无隐式转换、花括号块等）。

所有语法决策由用户 2026-09-10 逐项确认。

---

## M2 范围

| 特性 | 状态 |
|------|------|
| struct | 纳入 |
| enum | 纳入 |
| 静态数组 `[N]T` | 纳入（原计划 M4，提前到 M2） |
| 元组 `<T1,T2>` | 纳入 |
| switch | 纳入（if 语法糖） |
| do-while | 纳入 |
| type 别名 + 类型计算 | 纳入 |
| sizeof / alignof / typeof | 纳入 |
| 函数类型 `func(...)->ret` | 纳入（无函数体的类型值） |
| 位运算复合赋值 `&= \|= ^= <<= >>=` | 纳入 |
| 三元表达式 `? :` | 纳入 |
| tagged union / cunion | **移出 M2**（鸭子类型协议复杂，后续里程碑） |
| slice | **移出 M2** |
| goto / 逗号运算符 | **不要** |

---

## 语法设计决策

### 1. 复合值构造 `.<type>{...}`

```
.Point{ .x = 1, .y = 2 }       // struct：具名字段 .field = value
.[1]i32{ 0 }                    // 数组类型 [N]T
.<i32, i32>{ 0, 1 }            // 元组类型 <T1,T2>
.{ .x = 1, .y = 2 }           // 无类型 + 具名字段 → 匿名结构体
.{ 0, 1 }                      // 无类型 + 匿名字段 → 元组
```

- type 不带方括号，除非类型本身是数组/元组
- 具名字段：`.x = 1`；匿名字段：直接给值
- **无类型 + 具名字段**：临时生成匿名结构体对象，靠鸭子类型（字段布局兼容）赋给左值
- **无类型 + 匿名字段**：生成元组
- `.<type>{...}` 是通用表达式，可嵌套任何表达式位置

**命名构造 vs 匿名构造**：
- `var p = .Point{...}` — 命名构造，`typeof(p) == Point`
- `var p: Point = .{ .x = 1 }` — 匿名构造 + 左值标注，鸭子类型是**运行时拷贝**（编译期检查布局兼容，运行时逐字段拷贝）

### 2. struct 定义

```
struct Point { x: i32; y: i32; };
```

- 字段用分号 `;` 分隔
- **鸭子类型协议**：成员名、类型、顺序相同 = 兼容；类型兼容指事实相等（布局大小、字段完全相等、kind 一致）
- **空 struct `struct {}` 与所有结构体/数组/元组兼容**（万能兼容）
- 编译期检查布局兼容，运行时字段拷贝

### 3. enum（严格与底层类型分离）

```
enum Color:i32 { Red = 1, Green = 2, Blue = 3 }
var c: Color = Color::Red;            // variant 用 :: 命名空间访问
var val: i8 = c as i32 as i8;         // 必须两次 as，不允许 c as i8
```

- 底层类型显式标注（`:i32`）+ 每个 variant 显式值，**类型和值都不允许省略**
- variant 访问用 `Color::Red`（`::`，非 `.`）
- **enum 与底层类型严格分离**：`Color extends i32 = false`，不隐式转换
- enum 只参与 enum 运算；底层值只是内存中的表达方式，非语言层面
- 唯一与底层的交互：`as` 到底层类型，再 `as` 到目标宽度（`c as i32 as i8`），**禁止跳步**
- enum 类型参与类型计算（extends/== 基于 enum 自身）
- `Color::Red == c` — variant 之间判等

### 4. switch = if 语法糖

```
switch(cond) {
    (a, b)->{ }     // 匹配 a 或 b（逗号分隔 = || 链，惰性求值）
    (c, d)->{ }     // 后续分支
    default->{ }    // 兜底
}
```

- `switch(cond)` 条件带括号；分支 `(模式列表)->{块}`
- **无 fallthrough**；default 兜底
- 条件可以是**运行时表达式**（不限制为常量）
- 模式目前仅值匹配（`val == a || val == b`），待确认是否支持解构绑定
- **编译期 desugar** 为嵌套 if 链（`var __switch_N = cond; if (...) {} else if ...`）

### 5. type 别名 = 类型计算表达式

```
type MyInt = i32;
type MyInt2 = MyInt extends i32 ? i32 : MyInt;   // 类型计算
type F = func(i32, i32)->i32;                     // 函数类型
```

- clux 支持类型计算；无泛型时支持 `extends`（类型兼容判断）和 `==`（类型相等）
- **类型/值三元完全统一**：类型右值当普通表达式执行，只判断结果是否是 type value
- **一切类型的右值槽位都视作表达式**：var 类型标注、函数参数/返回类型、cast 目标、`.<type>{}` 构造的类型位
- 需要统一的类型表达式求值器（`resolve_type` 从 `strslice_t` 升级为 `ast_node_t*`）

### 6. 数组 `[N]T`

```
var arr = .[3]i32{ 1, 2, 3 };
var mat: [2][3]i32 = ...;   // 多维 = [2]([3]i32)
```

- 边界 N 是**编译期常量立即值**（M2 不支持泛型/编译期计算，`[2+3]i32` 暂不支持）
- 多维天然支持：`[2][3]i32` 等价于 `[2]([3]i32)`，后者语法完全正确
- **元组 ↔ 数组匿名互转**：布局兼容时（如 `<i32,i32>` ↔ `[2]i32`）

### 7. do-while

```
do { ... } while (cond);    // 后置条件循环
```

### 8. sizeof / alignof / typeof

```
sizeof(T)        // 返回 u64
sizeof(expr)     // 对表达式做 shadow 计算，不真实执行
alignof(T)       // 返回 u64
typeof(expr)     // 返回 type value
```

- 参数只做 **shadow 计算**，不作真实执行

### 9. 位运算复合赋值

```
&=  |=  ^=  <<=  >>=
```

全部纳入 M2。

---

## 关键架构决策

### 1. 类型表达式 = 普通表达式

`[N]T`、`<T1,T2>`、`func(...)->ret`、命名类型、`extends`/`==`/`? :` 都走统一 Pratt 解析，结果为 type value。

- 新增 `parse_type_expr` 入口与 `parse_expr` 共享 Pratt 核心
- `resolve_type` 从 `strslice_t` 升级为 `ast_node_t*` — **M2 的基础**
- M1 简单命名类型走 fast path 保持性能

### 2. value_t 新增 `is_own` 字段 + 借用数据访问

```
struct value_t {
    const type_t *type;
    void *data;
    bool is_shadow;
    bool is_own;      // M2 新增
};
```

- `is_own=true`：value 拥有数据，负责 disposal（当前 M1 行为）
- `is_own=false`：value 借用数据，**不负责 disposal**，data 指向其他 value 的数据块内部
- **struct/array 内存布局与 C 语言一致**：字段/元素在连续内存中按固定偏移排列
- **`GET_FIELD`**：结果 value 的 `is_own=false`，`data = struct_value.data + field_offset`
- **`SET_FIELD`**：通过 `data + offset` 直接写入父结构体的数据块
- **clone 语义**：`is_own=false` → 浅拷贝（只复制指针）；`is_own=true` → 深拷贝（走 vtable clone）
- **dispose 语义**：`is_own=false` → 跳过 disposal（借用者不拥有数据）
- scope 所有权保证生命周期：只要父 value 在 scope 中存活，借用引用就有效

### 3. switch = 编译期 desugar

解析为嵌套 if 链（`var __switch_N = cond; if (...) {} else if ...`），无独立 switch 语义。

### 4. enum 严格分离

`Color extends i32 = false`，只能 `as` 底层类型，需两次 as（`c as i32 as i8`）。

### 5. 类型名迁移

`strslice_t type_name` → `ast_node_t *type_expr`，所有类型槽位统一。

### 6. 复合类型

`struct_type_t`/`array_type_t`/`tuple_type_t`/`enum_type_t` 全跟 `func_type_t` C 继承，各列 interning 池 + 独立 vtable。

- `VTABLE_STRUCT.implicit_cast`：鸭子类型检查，空 struct 万能兼容
- `VTABLE_ARRAY.implicit_cast`：Array↔Tuple，同元素数+类型
- `VTABLE_TUPLE.implicit_cast`：Tuple↔Array
- `VTABLE_ENUM.implicit_cast`：仅同枚举（严格分离）；`explicit_cast`：仅到声明底层类型

### 7. 鸭子类型协议

成员/类型/顺序相同 = 兼容；空 struct 万能兼容；编译期检查，运行时字段拷贝。

---

## 实现阶段

### Phase 0: Lexer + AST 基础

**Lexer**
- 关键字：`struct`, `enum`, `type`, `switch`, `default`, `do`, `sizeof`, `alignof`, `::`, `?`
- 3-char 符号：`<<=`, `>>=`
- 2-char：`&=`, `|=`, `^=`

**AST kinds**
- `AST_TYPE_NAME`, `AST_TERNARY`, `AST_STRUCT_DEF`, `AST_ENUM_DEF`, `AST_TYPE_DEF`, `AST_ARRAY_TYPE`, `AST_TUPLE_TYPE`, `AST_FUNC_TYPE`, `AST_CONSTRUCT`, `AST_SWITCH`, `AST_DO_WHILE`, `AST_SIZEOF`, `AST_ALIGNOF`, `AST_TYPEOF`, `AST_PATH`

**parse_type_expr**
- `parse_type_expr(p)` → 解析类型表达式，产生 `AST_TYPE_NAME` / `AST_ARRAY_TYPE` / `AST_TUPLE_TYPE` / `AST_FUNC_TYPE`
- 与 `parse_expr` 共享 Pratt 核心

### Phase 2: VM 类型系统扩展

- `type_compatible(vm, from, to)` — 鸭子类型检查
- `vm_register_type(vm, type, name)` — 用户类型注册
- 新类型：`struct_type_t`, `array_type_t`, `tuple_type_t`, `enum_type_t`
- interning 池 + 独立 vtable
- value_t 新增 `is_own` 字段 + 借用数据 dispose/clone 语义

### Phase 3: Sema 扩展

- `resolve_type(sema, ast_node)` → `ast_node_t*`（从 `strslice_t` 升级）
- 类型计算：`extends`/`==` → bool shadow，`? :` 按条件选分支
- Pass 1/2 扩展：收集类型定义名称、解析类型内部结构
- sema_expr：`AST_CONSTRUCT`/`AST_MEMBER`/`AST_INDEX`/`AST_PATH`/`AST_SIZEOF`/`AST_ALIGNOF`/`AST_TYPEOF`/`AST_TERNARY`
- sema/stmt：`AST_DO_WHILE`/`AST_SWITCH` + 位运算复合赋值

### Phase 4: 构造 + 访问

**新字节码**
- `BCODE_MAKE_STRUCT`/`BCODE_MAKE_ARRAY`/`BCODE_MAKE_TUPLE`
- `BCODE_GET_FIELD`/`BCODE_SET_FIELD`（借用数据：is_own=false, data=parent.data+offset）
- `BCODE_INDEX_GET`/`BCODE_INDEX_SET`
- `BCODE_GET_ENUM_VARIANT`
- `BCODE_SIZEOF`/`BCODE_ALIGNOF`/`BCODE_TYPEOF`

**Parser**
- `parse_construct`：`. <type_expr> { field_inits }` 或 `. { field_inits }`
- `parse_path`：`Color::Red` → `AST_PATH`，`::` 作为后缀操作符

**Compiler**
- 所有新 AST 节点的编译 case

### Phase 5: 控制流 + 表达式补全（与 Phase 4 可并行）

- `parse_ternary`（`? :`），`parse_switch`，`parse_do_while`
- 位运算复合赋值扩展
- Sema + Compiler desugar/编译

### Phase 6: 集成 + 测试

- 新 examples：`structs.cx`, `arrays.cx`, `enums.cx`, `tuples.cx`, `switch.cx`, `type_computation.cx`, `do_while.cx`, `sizeof.cx`
- `npm run check:highlight` + `npm run compile` + `./clux_test`
- 8+ examples 端到端

---

## 关键路径

```
Phase 0 (Lexer/AST) → Phase 2 (VM 类型) → Phase 3 (Sema) → Phase 4 (构造/访问) → Phase 5 (控制流/表达式) → Phase 6 (集成)
```

Phase 0 是基础（新关键字 + `parse_type_expr`）；Phase 3 是枢纽（`resolve_type` + `sema_expr`）；Phase 4 是最大工程（构造 + 访问 + 新字节码）。

## 文件改动摘要

**新增 ~40 个文件**（parse_type_expr.c, type_struct.c, type_array.c, type_tuple.c, type_enum.c, parse_construct.c, parse_path.c, parse_ternary.c, parse_switch.c, parse_do_while.c, ...）
**修改 ~25 个文件**（lexer.c, parse_expr.c, sema.c, compile_expr.c, compile_stmt.c, exec.c, ast_kind.h, value.c, type.h, ...）
