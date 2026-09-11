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
| comptime 关键字（`comptime var` / `comptime func`） | **纳入**（2026-09-11，见 §9） |
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

### 8. sizeof / alignof / typeof（SEMA→CTFE 桥梁）

```
sizeof(T)        // 返回 u64
sizeof(expr)     // 对表达式做 shadow 计算，不真实执行
alignof(T)       // 返回 u64
typeof(expr)     // 返回 type value
```

- 参数只做 **shadow 计算**，不作真实执行
- **这三个运算符本身就是编译期运算，是 SEMA→CTFE 的桥梁**（2026-09-11 补充）：
  - 操作数只做 shadow 求值（仅取类型），输出**真实编译期常量**（sizeof/alignof → u64，typeof → type value）
  - `var a:i32 = 1; var b:[sizeof(a)]i32 = .{};` 合法——`a` 是 shadow 值，但 `sizeof(a)` **不是** shadow：从 `a` 的类型 i32 产出真实常量 4，喂给数组边界槽位
  - 因此这三个运算符**不需要 ctfe 真实求值操作数**：在 sema shadow 路径内即可完成（shadow 操作数 → 真实常量），产物直接消费于类型槽位（数组边界 N、type 计算等），是 shadow 世界 → 真实值世界的天然桥梁
  - ctfe 遇到这些节点时遵循同一规则：操作数按类型（shadow 语义）求值，结果产生真实常量

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

### 2. value_t 新增 `is_own` 字段 + 字段借用引用

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
- **struct_type_t 持有 field_t 数组**：`field_t { name; offset; type; }`，offset 在类型 intern 时按 C 对齐规则一次性布局（`offset_i = align_up(prev_end, align_i)`，`size = align_up(last_end, max_align)`），字段访问 O(1)
- **`t.b` 是借用引用（partial reference into struct memory）**：天然是左值
  - **读** `var b = t.b` → 产生 `is_own=false` 借用值，`data = (uint8_t*)t.data + offsetof(b)`
  - **写** `t.b = 123` → `BCODE_SET_FIELD` 经 `data + offset` 直接写入父数据块（与 C `t.b=123` 一致）
- **`GET_FIELD`**：结果 `is_own=false`，`data = parent.data + field_offset`，`is_shadow` 沿字段传播
- **clone 语义**：`is_own=false` 借用值被 clone → materialize 深拷贝（分配新块）；`is_own=true` → 正常深拷贝
- **dispose 语义**：`is_own=false` → 跳过 disposal（借用者不拥有数据）
- **嵌套字段** `t.a.b.c`：链式借用，每层 `is_own=false`，data = 最外层 data + 累计偏移，零拷贝
- **数组下标** `arr[i]`：同理 `data = arr.data + i * elem_size`，`is_own=false`（array_type_t 只需 elem 类型 + bound）
- **RET 借用值 materialize**：函数返回借用值（字段/数组元素）时产生一次 clone（深拷贝），因为函数 scope 销毁后父 value 不再存活（`func f():i32 { var t:Test; return t.b; }`）
- scope 所有权保证生命周期：只要父 value 在 scope 中存活，借用引用就有效

### 3. 复合类型/值构造统一多步协议（非一次构造）

所有复合类型构造与值构造（初始化）都是**多步字节码协议**，不是一次完成。统一模式：`push_xxx`（压构造器，**无参数，一律注册全局 type 表**）→ 成分逐步定义（成分本身是表达式，`load` 可以是任意构造表达式的结果，嵌套天然支持）→ `seal`（冻结 + 布局计算）。

**struct 类型** `struct Test { a:i32; b:i32; };`
```asm
push_struct          ; 压入 struct 构造器，注册全局 type 表
load "i32"           ; 字段类型表达式入栈
define_field "a"     ; 消费栈顶类型值，追加字段 a
load "i32"
define_field "b"
seal                 ; 冻结字段表，计算 C 布局（offset/align/size），转不可变 struct_type_t
push_undefined; define "Test"   ; define 永远双弹 [value, type-spec]；undefined 占类型位，类型从值推断
```

**数组类型** `[3]i32`
```asm
push_array           ; 压入 array 构造器，注册全局 type 表
load "i32"           ; 元素类型表达式入栈
define_elem 3        ; 消费栈顶类型值设元素类型，带立即数边界 3（编译期常量）
seal                 ; 冻结 + 布局（size = bound * elem_size）
```

**元组类型** `<i32, i32>`
```asm
push_tuple           ; 压入 tuple 构造器，注册全局 type 表
load "i32"
define_elem          ; 追加匿名字段（位置 0）
load "i32"
define_elem          ; 追加匿名字段（位置 1）
seal                 ; 冻结 + C 布局（与 struct 同布局规则）
```

**type 别名变体**（struct 为例，数组/元组同理）——**与 struct 定义完全等价**：
```asm
; type Test = struct { a:i32; b:i32; };
push_struct; load "i32"; define_field "a"; load "i32"; define_field "b"; seal;
push_undefined; define "Test"   ; 与变体 1 完全等价（右值表达式求值 = 单值类型值 + undefined 类型位）
```

**类型引用**：命名类型统一 `load "Test"`（从全局 type 表取类型值）；内联类型表达式（`[N]T`/`<T1,T2>`）在类型槽位直接构造类型值。
```asm
; var data:Test = .{ .a = 1, .b = 2 };
load "Test"          ; 命名类型引用：从全局 type 表取出 Test 类型值压栈
; ... .{} 值构造 ...（见下）
define "data"
```

**值构造（初始化）** `.<type>{...}` / `.{...}` 同样多步：

带类型位 `var p = .Point{ .x = 1, .y = 2 };`：
```asm
load "Point"         ; 类型位：命名类型引用
push 1               ; 字段值按类型字段序压栈（字段名编译期重排/校验，不产生运行时指令）
push 2
construct 2          ; 值构造完成：弹出 2 个字段值 + 类型位，按 Point 布局分配数据块写值 → value
define "p"
```

匿名具名字段 `.{ .x = 1, .y = 2 }`（鸭子类型赋左值）——**先在栈上构造匿名类型**（等价于具名的 `load "XXX"`）：
```asm
push_struct          ; 在栈上构造匿名 struct 类型（等价于具名类型的 load "Test"）
load "i32"
define_field "x"
load "i32"
define_field "y"
seal                 ; 匿名 struct 类型值留栈顶
push 1               ; 字段值按匿名类型字段序压栈
push 2
construct 2          ; 弹出 2 个字段值 + 类型位，分配数据块写值 → value
```

匿名匿名字段 `.{ 0, 1 }`（→ 元组）：
```asm
push_tuple           ; 在栈上构造匿名 tuple 类型
load "i32"
define_elem
load "i32"
define_elem
seal                 ; 匿名 tuple 类型值留栈顶
push 0
push 1
construct 2          ; 弹出 2 个值 + 类型位，完成元组值构造
```

数组值构造 `.[1]i32{ 0 }`：
```asm
push_array; load "i32"; define_elem 1; seal  ; 内联构造数组类型 [1]i32（类型值留栈顶）
push 0
construct 1          ; 弹出 1 个元素值 + 类型位，完成数组值构造
```

**统一规则**：
- `push_struct`/`push_array`/`push_tuple`：压**类型**构造器，**无参数，一律注册全局 type 表**（构造期间即被登记，类型引用可解析）
- `define_field name` / `define_elem [bound]`：消费栈顶类型值追加成分
- **`seal` 只用于类型构造完成**：冻结 + 布局计算（C 对齐规则）
- **`construct N` 用于值构造完成**（新指令，非 seal）：N = 字段数量立即数，弹出 N 个字段值 + 类型位，分配数据块按布局写值，校验字段数
- **值构造类型位统一**：类型位永远是栈顶一个类型值——命名类型 = `load "Test"`，匿名类型 = 先 `push_xxx...seal` 在栈上构造类型值留栈顶（等价于具名 `load`）；随后字段值按类型字段序压栈 → `construct N`。**类型生成发生在类型构造阶段（push_xxx...seal），construct 不负责生成类型**
- **字段名纯编译期**：具名字段 `.field = v` 的字段名只在编译期用于重排值压栈顺序 + 字段存在性/缺失校验，**不产生运行时指令**（运行时按类型字段序写值，无 store_field）
- **缺失字段递归补全 0 值**（compiler 职责）：用户只提供部分字段时（`.{ .x = 1 }` 缺 y），编译器按类型字段序对**缺失字段递归生成该字段类型的 0 值构造字节码**，保证 `construct N` 的 N 个字段值齐全——基本类型压 0 立即数；复合类型（struct/array/tuple）递归构造零值对象（复用类型位 + 各子字段 0 值 + construct）。示例 `var p = .Point{ .x = 1 }`：
```asm
load "Point"; push 1; push 0; construct 2   ; y 缺失 → 补 i32 0 值
```
- **类型引用**：命名类型统一 `load "Test"`；内联类型表达式（`[N]T`/`<T1,T2>`）在类型槽位直接构造类型值。与类型定义（push_xxx）解耦
- **`define` 是唯一绑定指令**（`define_struct`/`DEFINE_FUNCTION` 已删除），**永远双弹 `[value, type-spec]`**（value 在底、类型说明符在顶，**无单弹分支**）：type-spec = type value（`load "T"`，显式类型）或 undefined（`push_undefined`，无标注 → define 从值推断类型）。`var a = 5` → `push_i32 5; push_undefined; define "a"`；`var a:i32 = 5` → `push_i32 5; load "i32"; define "a"`；**函数定义** → `CREATE_FUNC_TYPE argc; PUSH_FUNCTION entry_pc; push_undefined; define "add"`（函数值自带签名类型）；**函数参数绑定**（函数体开头倒序）→ 每参数 `push_undefined; define name`（从值推断，与 var 定义完全一致）。**两个变体完全等价**：`struct Test {...}` 与 `type Test = struct {...}` 字节码相同（`push_struct...seal; push_undefined; define "Test"`）
- 类型构造与值构造走同一套构造器求值协议
- 为类型计算打基础：`type T = <表达式>` 右值就是普通表达式求值

### 4. switch = 编译期 desugar

解析为嵌套 if 链（`var __switch_N = cond; if (...) {} else if ...`），无独立 switch 语义。

### 5. enum 严格分离

`Color extends i32 = false`，只能 `as` 底层类型，需两次 as（`c as i32 as i8`）。

### 6. 类型名迁移

`strslice_t type_name` → `ast_node_t *type_expr`，所有类型槽位统一。

### 7. 复合类型

`struct_type_t`/`array_type_t`/`tuple_type_t`/`enum_type_t` 全跟 `func_type_t` C 继承，各列 interning 池 + 独立 vtable。

- `VTABLE_STRUCT.implicit_cast`：鸭子类型检查，空 struct 万能兼容
- `VTABLE_ARRAY.implicit_cast`：Array↔Tuple，同元素数+类型
- `VTABLE_TUPLE.implicit_cast`：Tuple↔Array
- `VTABLE_ENUM.implicit_cast`：仅同枚举（严格分离）；`explicit_cast`：仅到声明底层类型

### 7. 鸭子类型协议

成员/类型/顺序相同 = 兼容；空 struct 万能兼容；编译期检查，运行时字段拷贝。

### 8. 编译期计算（CTFE）：直接运行 AST 的常量求值器

M2 需要**编译期类型计算**（type 别名、数组边界 `[N]T`、sizeof/alignof/typeof、enum 值），而当前表达式计算是运行期的（字节码 + VM）。因此新增 **CTFE（compile-time evaluation）**：直接解释 AST 表达式，产生**编译期确定的真实 value**（非 shadow）。

**架构定位**：独立模块（`src/ctfe/`），与 sema 的 shadow 类型检查（`sema_expr`）平行。sema 是全部表达式的主路径（纯类型检查）；ctfe 只在**需要编译期常量的槽位**被按需调用。两者共享 value 层 vtable 运算（`value_add` 等对真实值与 shadow 均可用）。

```
sema_expr（shadow 类型检查，全表达式主路径）
   │  需要编译期常量的槽位（M2）：
   │   · 数组边界 [N]T → ctfe 求值 N → u64
   │   · type 别名 = 类型计算表达式 → ctfe → type value
   │   · sizeof/alignof/typeof → SEMA 内完成（shadow 操作数 → 真实常量，见 §8），
   │     不经 ctfe 真实求值，产物直接喂类型槽位
   │   · enum variant 值 → ctfe → 常量
   ▼
ctfe_eval(sema, ast_node *expr) → value_t*（真实值）
   · 同步递归 AST 解释器（天然"不允许暂停恢复"）
   · 字面量 → 构造真实值；运算 → value_add 等 vtable（真实值路径）
   · 变量 lookup → ctfe 作用域链（参数/局部 var 绑定）；查不到 → error
   · 函数调用 → 仅解释 AST_FUNC_DEF（clux 函数体 AST）；非 AST 实体 → error
   · budget：步骤上限 + 递归深度上限（防死循环/无限递归）
   · 生命周期：临时 vm scope（push/pop），返回值 clone 到调用方 scope
```

**严格限制**（用户确认）：

| 限制 | 实现 |
|------|------|
| 不允许暂停恢复 | 同步递归下降，无 yield/resume、无跨调用挂起状态 |
| 必须编译期求值 | 遇到运行期依赖（变量非常量、副作用外逃）→ `error("not a compile-time constant")`；budget 超限报错 |
| 禁止调用 FFI | **规则记录，暂不实现检测**：CTFE 禁止调用 FFI 函数。当前 M1 无 FFI；未来 FFI 函数与编译器内置函数将以**专门实体表示**（非 AST_FUNC_DEF，也不以 `ast==NULL` 区分），届时 CTFE 对 FFI 调用报错。本设计只记录该约束 |

**编译期计算与 CTFE 的边界**（2026-09-11 用户补充）：

- **shadow 值不可用于编译期计算**：常量槽位（数组边界 N、type 别名计算、enum 值）需要**真实编译期常量值**。`var a = 1; var b:[a]i32 = .{};` **非法**——`a` 在 sema 中是 shadow 值（仅类型、无数据），边界槽位取不到真实值（除非 `comptime var a`，见 §9）
- **编译期函数调用实参同样受限**：`var b:[getLength(a)]i32 = .{};` **非法**——ctfe 解释 `getLength` 函数体需要真实实参值，shadow 无法提供
- **唯一例外**：sizeof/alignof/typeof（§8 桥梁）——操作数只取类型，shadow 可参与，运算符自身产出真实常量
- **sema 路径检查**：sema 在常量槽位求值前，必须检查表达式是否依赖 shadow 值 / 运行期值（标识符查找、函数调用实参传播）→ 是则**在 sema 阶段报诊断**（`compile-time constant required`），不得漏到 ctfe 才报笼统错误

**决策点定稿**（2026-09-11 用户逐项确认）：

1. **独立 ctfe 模块**（非 sema_expr 双模式）：职责清晰，shadow 语义（`is_shadow`/`data=NULL` 纯类型检查）不动，ctfe 约束（budget/FFI）独立演进。代价：表达式求值逻辑与 sema_expr 部分重复
2. **普通函数：仅常量槽位可编译期调用**：非 comptime 函数在常量槽位（数组边界等）被调用时尝试编译期求值，失败报错；在普通表达式位置按运行期字节码调用。CTFE 只解释 `AST_FUNC_DEF`（clux 函数）；FFI/编译器内置等非 AST 实体不在可调范围
3. **支持局部赋值与循环**：函数体内 var 定义、赋值、if、while/for 均可编译期执行（作用于局部常量环境），编译期纯函数表达力完整
4. **comptime 关键字直接加入 M2**（2026-09-11 推翻"预留"决策，基础设施已就绪）：`comptime var` / `comptime func` 显式声明编译期实体，见下方 §comptime

### 9. comptime 关键字（M2 纳入）

`comptime` 修饰**变量定义与函数定义**，显式声明编译期实体。核心机制是 **vm 的 comptime 状态**（2026-09-11 用户补充统一状态模型）。

**vm->comptime 状态**（统一求值模式开关）：
- `vm->comptime = true`：表达式**必须编译期求值**（ctfe 真实值），求值失败 → 编译报错
- `vm->comptime = false`：只做类型检查（shadow 语义），普通运行期路径
- **comptime var / comptime func 是主动标注的编译期上下文**：进入这些实体时 `vm->comptime = true`
- **类型表达式是隐式的自动标注**：类型表达式槽位（数组边界 `[N]T`、type 别名 rhs、`sizeof/alignof/typeof` 参数、`.<type>{}` 类型位）解析/求值时自动 `vm->comptime = true`

```
comptime var a = add(1,2);           // 右值必须编译期可计算（否则编译错误）
comptime func add(a:i32,b:i32):i32 { return a+b; }
var b = add(1,2);                    // add 是 comptime func → vm->comptime=true → 折叠为 var b = 3
var arr:[N]i32 = .{};                // N 是 comptime var（全局/局部），边界槽位自动 comptime
```

**comptime var**：
- 右值在 `vm->comptime = true` 下**必须编译期可计算**（ctfe 求值成功），否则编译错误
- 求值成功后**后续语句中标识符 `a` 直接替换为值**（常量替换，类似 `#define`/constexpr 变量）：sema 阶段把 `a` 的引用解析为折叠后的常量节点（标量 → `AST_INT_LIT` 等，复合 → `AST_CT_CONST`）
- **作用域：全局 + 局部都支持**（推翻"无全局常量"决策）：
  - 顶层 `comptime var N = 5;` 是全局编译期常量，任意函数可引用（`var arr:[N]i32 = .{};` 合法）
  - 函数体内 `comptime var` 是局部编译期常量（`return a;` 替换为 3）

**comptime func**：
- 调用时 `vm->comptime = true` **强制编译期求值**：`var b = add(1,2)` 在编译期调用，得到编译期值并折叠为 `var b = 3`（AST 写回）；求值失败 = 编译错误
- 与普通函数的关系：普通函数仅在隐式 comptime 上下文（常量槽位）被尝试编译期求值，普通表达式位置按运行期字节码调用；comptime func 在**任何位置**都强制编译期求值
- 函数体遵循 CTFE 解释规则（支持 var/赋值/if/while/for/return），保证纯编译期可执行

**实现**：`comptime` 是语法糖标记——解析器接受 `comptime` 前缀（`AST_VAR_DEF` / `AST_FUNC_DEF` 加 `is_comptime` 标志）；sema 遇到 `comptime var` 置 `vm->comptime = true` 求值右值并折叠写回，引用点查全局/局部编译期常量表直接替换；`comptime func` 在符号表中标记，调用点置 `vm->comptime = true` 强制走 ctfe；类型表达式槽位自动置位。求值器对 `vm->comptime` 的检查统一在表达式求值入口（ctfe_eval）：状态为 true 且无法编译期求值 → error。

**常量折叠写回 AST**（sema 阶段，CTFE 的消费机制）：

- 常量槽位求值成功后，将结果**写回替换 AST 节点**：折叠后的常量（如 `[1+2]i32` → `[3]i32`）以 AST 字面量节点（`AST_INT_LIT` 等）原地替换原表达式节点，后续 sema 类型检查与编译器直接消费常量，**不再生成运行期计算字节码**
- 替换方式：父节点持有子表达式指针的槽位（如 `ast_array_type_t->bound`、type 别名 rhs、enum 值）由 sema 在求值成功后指针替换；新常量节点经 arena 分配
- 折叠范围：**类型槽位必须折叠**（编译期类型计算依赖）；普通表达式（如 `var x = 1 + 2`）尝试折叠，成功则写回（编译期优化），失败照常走 shadow 检查 + 运行期字节码
- 安全性：ctfe 求值成功 ⟺ 表达式纯（无副作用/无运行期依赖），"ctfe 成功 → 折叠写回"不改变语义
- 传播：ctfe 作用域链内的绑定（参数、局部 var）携带真实常量值，同一函数内后续表达式可继续折叠
- **写回不限于字面量，需支持复杂类型值**（comptime 已在 M2 纳入，立即生效）：编译期求值结果可能是 struct/array/tuple 等复合常量（如 `.[3]i32{1,2,3}`、匿名 struct 常量），此时以字面量节点替换不成立——需新增**编译期常量对象 AST 节点**（`AST_CT_CONST`，携带类型 + 值数据），承载任意类型的折叠结果。标量常量槽位（边界 N/enum 值等）用 `AST_INT_LIT` 等即可；`comptime var` 的复合常量写回与 `comptime func` 返回值替换均依赖 `AST_CT_CONST`

**CTFE 求值器结构**：

- `ctfe_eval(sema, node)`：表达式求值入口，返回真实 value（归调用方 scope）
- `ctfe_eval_stmt`：语句级解释器（var/return/if/while/for/block/表达式语句），服务于 CTFE 函数体
- `ctfe_ctx`：求值上下文（budget 计数器：步骤 + 递归深度；当前临时 scope；sema 指针）
- 函数调用：`sema_lookup` 查符号 → 仅当 `sym->ast` 为 `AST_FUNC_DEF`（clux 函数）→ 绑定参数到新临时 scope → `ctfe_eval_stmt` 解释函数体 → 返回 return 值 clone 给调用方；递归共享同一 `ctfe_ctx` budget。非 `AST_FUNC_DEF` 实体（FFI/编译器内置，未来以专门表示存在）不在 CTFE 可调范围
- 错误统一 `value_make_error` + 语义化消息，sema 调用方翻译为诊断（`compile-time constant required`）

**M2 消费点**：数组边界 N、type 别名计算、enum 值、`.[N]T{...}` 构造边界——经 ctfe 求值后读数值或 type value。sizeof/alignof/typeof **不经 ctfe**（SEMA→CTFE 桥梁，见 §8）：操作数 shadow 求值取类型，运算符自身产出真实常量直接消费。

---

## 实现阶段

### Phase 0: Lexer + AST 基础

**Lexer**
- 关键字：`struct`, `enum`, `type`, `switch`, `default`, `do`, `sizeof`, `alignof`, `comptime`, `::`, `?`
- 3-char 符号：`<<=`, `>>=`
- 2-char：`&=`, `|=`, `^=`

**AST kinds**
- `AST_TYPE_NAME`, `AST_TERNARY`, `AST_STRUCT_DEF`, `AST_ENUM_DEF`, `AST_TYPE_DEF`, `AST_ARRAY_TYPE`, `AST_TUPLE_TYPE`, `AST_FUNC_TYPE`, `AST_CONSTRUCT`, `AST_SWITCH`, `AST_DO_WHILE`, `AST_SIZEOF`, `AST_ALIGNOF`, `AST_TYPEOF`, `AST_PATH`
- `AST_CT_CONST`（编译期常量对象：携带类型 + 值数据，承载复合常量写回）
- `AST_VAR_DEF` / `AST_FUNC_DEF` 新增 `is_comptime` 标志（comptime 语法糖，不建独立节点）

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
- **comptime 处理**：`comptime var` 右值在 `vm->comptime = true` 下求值 + 写回全局/局部编译期常量表；`comptime func` 符号表标记 + 调用点强制编译期；类型表达式槽位自动置 `vm->comptime = true`（见 §9）

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
