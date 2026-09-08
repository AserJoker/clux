# VM 隐式转换与显式转换设计

## 总览

```
隐式转换 (implicit_cast): 编译器自动插入，不丢精度（widening）
显式转换 (explicit_cast): 程序员手写 (T)expr，允许窄化/丢精度
```

## 类型分类

| 类别 | 类型 | 存储类型 | vtable |
|------|------|---------|--------|
| 有符号整数 | i8, i16, i32, i64 | int64_t | VTABLE_INT_SIGNED |
| 无符号整数 | u8, u16, u32, u64 | uint64_t | VTABLE_INT_UNSIGNED |
| 浮点 | f32, f64 | double | VTABLE_FLOAT |
| 布尔 | bool | bool | VTABLE_BOOL |
| 字符串 | str | string_t* | VTABLE_STR |
| 空类型 | void | — | VTABLE_VOID |
| 元类型 | type | const type_t* | VTABLE_TYPE |
| 函数 | func | func_t* | VTABLE_FUNC |
| 错误 | error | error_data_t | VTABLE_ERROR |

## 有符号/无符号 vtable 差异

| 运算 | VTABLE_INT_SIGNED | VTABLE_INT_UNSIGNED |
|------|-------------------|---------------------|
| div | 有符号除法（截断向零） | 无符号除法（uint64_t） |
| mod | 有符号取模 | 无符号取模（uint64_t） |
| shr | 算术右移（符号扩展） | 逻辑右移（零填充） |
| lt/le/gt/ge | 有符号比较 | 无符号比较（uint64_t） |
| eq/ne | 相同（位比较） | 相同 |
| add/sub/mul | 相同 | 相同 |
| band/bor/bxor/bnot | 相同 | 相同 |
| shl | 相同 | 相同 |
| neg | 有符号取反 | 无意义（显式才允许？或返回 error） |
| display | `%+" PRId64` | `%+" PRIu64` |

---

## 优先级排序（用于类型协商）

```
高 → 低：
  f64 > f32 > u64 > i64 > u32 > i32 > u16 > i16 > u8 > i8 > bool
```

规则：
- 同宽度 unsigned > signed（匹配 C 语义）
- 浮点 > 整数
- 宽 > 窄

---

## 隐式转换矩阵 (implicit_cast)

**原则：只允许不丢精度的拓宽转换**

| 源 \ 目标 | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64 | f32 | f64 | bool | str | void | type | func | error |
|-----------|----|-----|-----|-----|----|-----|-----|-----|-----|-----|------|-----|------|------|------|-------|
| **i8**  | = | OK | OK | OK | OK | OK | OK | OK | OK | OK | — | — | — | — | — | — |
| **i16** | — | =  | OK | OK | —  | OK | OK | OK | OK | OK | — | — | — | — | — | — |
| **i32** | — | —  | =  | OK | —  | —  | OK | OK | OK | OK | — | — | — | — | — | — |
| **i64** | — | —  | —  | =  | —  | —  | —  | OK | OK | OK | — | — | — | — | — | — |
| **u8**  | — | —  | —  | —  | =  | OK | OK | OK | OK | OK | — | — | — | — | — | — |
| **u16** | — | —  | —  | —  | —  | =  | OK | OK | OK | OK | — | — | — | — | — | — |
| **u32** | — | —  | —  | —  | —  | —  | =  | OK | OK | OK | — | — | — | — | — | — |
| **u64** | — | —  | —  | —  | —  | —  | —  | =  | OK | OK | — | — | — | — | — | — |
| **f32** | — | —  | —  | —  | —  | —  | —  | —  | =  | OK | — | — | — | — | — | — |
| **f64** | — | —  | —  | —  | —  | —  | —  | —  | —  | =  | — | — | — | — | — | — |
| **bool** | — | — | — | — | — | — | — | — | — | — | =  | — | — | — | — | — |
| **str**  | — | — | — | — | — | — | — | — | — | — | — | =  | — | — | — | — |
| **type** | — | — | — | — | — | — | — | — | — | — | — | — | — | =  | — | — |
| **func** | — | — | — | — | — | — | — | — | — | — | — | — | — | — | =  | — |
| **error**| — | — | — | — | — | — | — | — | — | — | — | — | — | — | — | =  |

**规则汇总：**

1. **有符号 → 更宽有符号**：i8→i16/i32/i64, i16→i32/i64, i32→i64
2. **有符号 → 同宽或更宽无符号**：i8→u8/u16/u32/u64, i16→u16/u32/u64, i32→u32/u64, i64→u64
3. **无符号 → 更宽无符号**：u8→u16/u32/u64, u16→u32/u64, u32→u64
4. **无符号 → 有符号：禁止**（可能溢出，需显式转换）
5. **整数 → 浮点**：任意整数 → f32/f64
6. **f32 → f64**：拓宽
7. **bool ↔ 数字：禁止**（只能显式）
8. **str：不参与任何转换**
9. **同类型**：由 `value_implicit_cast` 提前 short-circuit（直接 clone）

---

## 显式转换矩阵 (explicit_cast)

**原则：允许窄化/丢精度，覆盖隐式全部 + 以下额外场景**

| 源 \ 目标 | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64 | f32 | f64 | bool | str | void | type | func | error |
|-----------|----|-----|-----|-----|----|-----|-----|-----|-----|-----|------|-----|------|------|------|-------|
| **i8**  | = | I | I | I | I | I | I | I | I | I | E | — | — | — | — | — |
| **i16** | E | = | I | I | E | I | I | I | I | I | E | — | — | — | — | — |
| **i32** | E | E | = | I | E | E | I | I | I | I | E | — | — | — | — | — |
| **i64** | E | E | E | = | E | E | E | I | I | I | E | — | — | — | — | — |
| **u8**  | E | E | E | E | = | I | I | I | I | I | E | — | — | — | — | — |
| **u16** | E | E | E | E | E | = | I | I | I | I | E | — | — | — | — | — |
| **u32** | E | E | E | E | E | E | = | I | I | I | E | — | — | — | — | — |
| **u64** | E | E | E | E | E | E | E | = | I | I | E | — | — | — | — | — |
| **f32** | E | E | E | E | E | E | E | E | = | I | E | — | — | — | — | — |
| **f64** | E | E | E | E | E | E | E | E | E | = | E | — | — | — | — | — |
| **bool** | E | E | E | E | E | E | E | E | E | E | = | — | — | — | — | — |
| **str**  | — | — | — | — | — | — | — | — | — | — | — | = | — | — | — | — |
| **type** | — | — | — | — | — | — | — | — | — | — | — | — | — | = | — | — |
| **func** | — | — | — | — | — | — | — | — | — | — | — | — | — | — | = | — |

`I` = 隐式允许（显式自然也允许）
`E` = 仅显式允许

**额外显式转换规则：**

1. **无符号 → 有符号**：u32→i32, u64→i64 等（位模式重解释，可能溢出）
2. **宽 → 窄（同符号）**：i64→i8 截断高位，f64→f32 丢精度
3. **浮点 → 整数**：向零截断
4. **数字 → bool**：0 → false，非 0 → true
5. **bool → 数字**：false → 0，true → 1
6. **str 不参与任何转换**

---

## 实现方案

### 1. VTABLE_INT_SIGNED / VTABLE_INT_UNSIGNED

```c
/* 有符号整数 vtable */
static value_t sint_div(vm_t *vm, value_t a, value_t b) {
    int64_t bv = value_as(a, int64_t);
    if (bv == 0) panic("division by zero");
    int64_t rv = value_as(a, int64_t) / bv;
    ...
}
static value_t sint_shr(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) >> value_as(b, int64_t); /* 算术右移 */
    ...
}
static value_t sint_lt(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) < value_as(b, int64_t)); /* 有符号比较 */
    ...
}

/* 无符号整数 vtable */
static value_t uint_div(vm_t *vm, value_t a, value_t b) {
    uint64_t bv = value_as(a, uint64_t);
    if (bv == 0) panic("division by zero");
    uint64_t rv = value_as(a, uint64_t) / bv;
    ...
}
static value_t uint_shr(vm_t *vm, value_t a, value_t b) {
    uint64_t rv = value_as(a, uint64_t) >> value_as(b, uint64_t); /* 逻辑右移 */
    ...
}
static value_t uint_lt(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, uint64_t) < value_as(b, uint64_t)); /* 无符号比较 */
    ...
}
```

**共享部分**（add/sub/mul/band/bor/bxor/bnot/shl/eq/ne/display）可提取为公共函数或宏。

### 2. 整数 implicit_cast / explicit_cast

```c
/* 有符号整数 implicit_cast：拓宽到更宽有符号/无符号/浮点 */
static value_t sint_implicit_cast(vm_t *vm, value_t v, const type_t *target);

/* 无符号整数 implicit_cast：只拓宽到更宽无符号/浮点，不允许转有符号 */
static value_t uint_implicit_cast(vm_t *vm, value_t v, const type_t *target);

/* 有符号整数 explicit_cast：implicit 全部 + 窄化 + bool */
static value_t sint_explicit_cast(vm_t *vm, value_t v, const type_t *target);

/* 无符号整数 explicit_cast：implicit 全部 + 转有符号 + 窄化 + bool */
static value_t uint_explicit_cast(vm_t *vm, value_t v, const type_t *target);
```

### 3. 浮点 implicit_cast / explicit_cast

```c
/* f32→f64 隐式 */
static value_t float_implicit_cast(vm_t *vm, value_t v, const type_t *target);

/* f64→f32, float→int, float→bool 显式 */
static value_t float_explicit_cast(vm_t *vm, value_t v, const type_t *target);
```

### 4. bool explicit_cast

```c
/* bool→int/float 显式 */
static value_t bool_explicit_cast(vm_t *vm, value_t v, const type_t *target);
```

### 5. 不参与转换的类型

str / void / type / func / error — 无 implicit_cast / explicit_cast 回调。

---

## 二元运算类型协商（后续步骤）

有了 implicit_cast 后，二元运算的流程变为：

```
a OP b
  1. result_type = promote(a.type, b.type)
  2. a' = implicit_cast(a, result_type)
  3. b' = implicit_cast(b, result_type)
  4. return a'.type->vtable->op(vm, a', b')  // 结果类型 = result_type
```

### promote 规则

```
rank: f64=10, f32=9, u64=8, i64=7, u32=6, i32=5, u16=4, i16=3, u8=2, i8=1, bool=0

promote(a, b):
  if a == b: return a
  ra = rank(a), rb = rank(b)
  if ra >= rb: return a.type    // 高 rank 胜出
  return b.type
```

注意：signed + unsigned 同宽度时，unsigned rank 更高，结果为 unsigned。
