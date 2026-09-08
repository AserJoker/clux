# VM 测试用例设计

## 总体策略

由整体到细节，分轮次推进。每轮聚焦一个层次，正常场景 + 异常场景全覆盖。
每轮产出测试代码、编译验证、全量 ctest 通过后进入下一轮。

---

## 第 1 轮：VM 生命周期 + 内置类型注册表（已完成）

**文件**: `vm_test.cpp` — `VmLifecycle` + `VmBuiltinTypes`

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| vm_new / vm_destroy | 创建后 non-null；销毁后 null；无泄漏 | — |
| 作用域链初始状态 | global → root → current(root) | — |
| push_scope / pop_scope | push 后 current 是 child；pop 后回到 parent | pop global scope → panic |
| 嵌套作用域 | 多层 push/pop，parent 链正确 | — |
| type_find | 全部 16 个内置类型按名查找命中 | 未知名称 → null；空串 → null |
| 类型 size/align | i32=8/f64=8/bool=1/void=0/str=ptr/func=ptr/error=16 | — |
| type_eq | 同类型 true，不同类型 false | — |
| type_as_value | type → value_t，type==type_type，data 指向原 type | — |

---

## 第 2 轮：Value 核心机制 + Scope 机制（已完成）

**文件**: `vm_test.cpp` — `ValueCore` + `ScopeMech`

### Value 核心机制

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| value_make | type/data 正确设置；value_as 读取数据 | — |
| value_is_void | type==NULL → true；有 type → false | — |
| value_clone auto-track | clone 后数据独立；track 到 current_scope | clone void → void |
| value_clone 独立性 | 修改 clone 不影响原始 | — |
| value_dispose | type 置 NULL，data 置 NULL | NULL 指针安全；type==NULL no-op |
| error 构造 | message/location 正确；auto-track | — |
| value_is_error | error → true；非 error → false | 已 dispose 的 value → false |
| 运算 error 短路 | a 是 error → 返回 a；b 是 error → 返回 b | — |
| 不支持运算 → error | str + str → error；-str → error | — |
| 正常运算分派 | int + int → int；int < int → bool | — |
| value_call 分派 | 非 callable → error | 参数含 error → 传播 |
| implicit_cast | 同类型 → clone；不支持 → error | void → error；error 传播 |
| explicit_cast | 不支持 → error | void → error；error 传播 |

### Scope 机制

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| scope_define | 绑定 name → value；返回 stored 指针 | NULL name → null；NULL scope → null |
| scope_lookup | 命中返回指针 | 未找到 → null |
| lookup 遍历 parent 链 | child 能找到 parent 的变量 | — |
| define 覆盖 | 同名重定义 → 新值 | — |
| scope_assign | 更新已有变量值 | 不存在 → false |
| assign 遍历 parent 链 | 在 child 中 assign parent 变量 | — |
| pop_scope 销毁 owned | clone 的 value 在 pop 后释放 | — |
| define in child pop 后消失 | pop 后 lookup 返回 null | — |
| 跨 scope clone | clone track 到目标 scope | — |
| str dispose on pop | str_clone 深拷贝，pop 时释放 string_t | — |
| scope_track | 直接 track 到 owned | track 后 data 所有权转移，不能 dispose raw |

---

## 第 3 轮：整数类型运算

**文件**: `type_int_test.cpp`

### 算术运算

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| add | i32 + i32 = i32；i64 + i64 = i64；正数+负数 | — |
| sub | 正常减法；结果为负 | — |
| mul | 正常乘法；乘以 0 | — |
| div | 正常除法；负数除法 | 除零 → panic |
| mod | 正常取模；负数取模 | 模零 → panic |
| neg | 正数取反；负数取反；0 取反 | — |

### 比较运算（返回 bool）

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| eq / ne | 相等/不等 | — |
| lt / le / gt / ge | 各种大小关系 | — |

### 位运算

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| band / bor / bxor | 基本位运算 | — |
| bnot | 按位取反 | — |
| shl / shr | 左移右移 | — |

### 跨宽度类型

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| 不同整数宽度 | i32 + i64 运算（同 VTABLE_INT，结果类型 = a.type） | — |
| u32 运算 | 无符号运算行为 | — |

### 生命周期

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| int clone | 深拷贝，独立 | — |
| int dispose | 正常释放 | — |
| 运算结果 track | 运算结果未 track（直接 value_make），需手动 dispose | — |

---

## 第 4 轮：浮点类型运算

**文件**: `type_float_test.cpp`

### 算术运算

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| add / sub / mul | f64 + f64 = f64；f32 + f32 = f32 | — |
| div | 正常除法 | 除零 → panic |
| mod | fmod 行为 | — |
| neg | 正/负/0 取反 | — |

### 比较运算

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| eq / ne | 精确相等/不等 | — |
| lt / le / gt / ge | 大小关系 | — |

### 跨宽度

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| f32 + f64 | 同 VTABLE_FLOAT，结果类型 = a.type | — |

### 不支持运算 → error

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| 位运算 | — | f64 & f64 → error；~f64 → error；shl/shr → error |

---

## 第 5 轮：Bool 类型运算

**文件**: `type_bool_test.cpp`

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| eq / ne | true==true；false!=true | — |
| lnot | !true = false；!false = true | — |
| 不支持算术 | — | bool + bool → error；-bool → error |
| 不支持位运算 | — | bool & bool → error |
| bool clone | 深拷贝 | — |
| bool display | — | （display 输出需捕获 stdout） |

---

## 第 6 轮：Str 类型运算

**文件**: `type_str_test.cpp`

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| str 构造 | string_from_cstr → value_make | — |
| str eq / ne | 相同内容 = true；不同内容 = false | 跨类型比较（str == int）→ false/true |
| str clone | 深拷贝 string_t | — |
| str dispose | 释放 string_t | — |
| 不支持算术 | — | str + str → error；-str → error |
| 不支持位运算 | — | str & str → error |
| 不支持比较 lt/le/gt/ge | — | str < str → error |

---

## 第 7 轮：Error 类型

**文件**: `type_error_test.cpp`

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| error 构造 | message 正确；location 可选 | — |
| error clone | 深拷贝 message + location | — |
| error dispose | 释放 message + location string | — |
| error 不参与运算 | — | error + error → 短路返回；error < int → 短路 |
| error display | — | （需捕获 stdout） |
| error 作为参数传播 | value_call 参数含 error → 传播 | — |
| error implicit_cast | — | error → 传播，不转换 |
| error 独立性 | clone 后修改不影响原始 | — |

---

## 第 8 轮：函数调用全流程

**文件**: `type_func_test.cpp`

### 基本调用

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| func 创建 + 调用 | cfunc 执行，返回正确值 | fn==NULL / cfunc==NULL → error |
| 无参函数 | argc=0 正常执行 | — |
| 有参函数 | 参数按位置传递 | — |

### 参数 safe_cast

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| 类型匹配 | 同类型直接 clone | — |
| 无类型声明 | params==NULL → 直接 clone | — |
| 部分声明 | 前 N 个有类型，其余无 | — |
| cast 失败 | — | 不支持转换 → error 传播 |
| 参数含 error | — | 任意参数是 error → 短路传播 |

### 返回值 safe_cast

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| 无 return_type | return_type==NULL → 直接借用 | — |
| 类型匹配 | result.type == return_type → 借用 | — |
| cast 失败 | — | 不支持转换 → error |
| 返回 error | cfunc 返回 error → is_error | — |
| 返回 void | result->type==NULL → ret 保持 void | — |

### 作用域管理

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| 闭包捕获 | closure_scope 中变量可访问 | — |
| root_scope 切换 | 函数内 root_scope 是模块作用域 | — |
| 局部作用域隔离 | push 的匿名 scope 在调用后销毁 | — |
| 现场恢复 | root_scope/current_scope 调用前后一致 | — |
| error 路径砍子树 | is_error → destroy_subtree 不走 pop_scope | — |

### 函数值生命周期

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| func_make_value | type==type_func，data 存 func_t* | — |
| func_clone | 浅拷贝指针（函数不可变） | — |
| func_dispose | func_destroy 释放 | — |
| func display | 有名 → `<func name>`；无名 → `<func>` | — |

---

## 第 9 轮：Type 类型 + Void 类型

**文件**: `type_type_test.cpp` + `type_void_test.cpp`

### Type 类型

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| type_as_value | type → value，type==type_type | — |
| type clone | 深拷贝（指针值拷贝） | — |
| type display | — | （需捕获 stdout） |
| 不支持运算 | — | type + type → error |

### Void 类型

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| void size=0 | value_alloc_data 返回 NULL | — |
| void display | — | （需捕获 stdout） |
| void clone | type!=NULL, data==NULL | — |
| 不支持运算 | — | void + void → error |

---

## 第 10 轮：跨类型交互 + 边界

**文件**: `vm_cross_test.cpp`

| 功能 | 正常场景 | 异常场景 |
|------|---------|---------|
| int + float 运算 | — | 不同 vtable → error（无 implicit_cast） |
| bool + int | — | → error |
| str == int | — | str eq 返回 false（类型不同） |
| 嵌套函数调用 | 函数 A 调函数 B，作用域正确 | — |
| 深层作用域栈 | 多层 push/pop，无泄漏 | — |
| error 贯穿多层 | 深层 error 传播到顶层 | — |
| allocator 无泄漏 | 所有测试 TearDown 验证 live_count==0 | — |

---

## 覆盖矩阵汇总

| 运算 | int | float | bool | str | error | type | void | func |
|------|-----|-------|------|-----|-------|------|------|------|
| add/sub/mul | OK | OK | error | error | short | error | error | error |
| div | OK+panic | OK+panic | error | error | short | error | error | error |
| mod | OK+panic | OK | error | error | short | error | error | error |
| neg | OK | OK | error | error | short | error | error | error |
| eq/ne | OK→bool | OK→bool | OK→bool | OK→bool | short | error | error | error |
| lt/le/gt/ge | OK→bool | OK→bool | error | error | short | error | error | error |
| band/bor/bxor | OK | error | error | error | short | error | error | error |
| bnot | OK | error | error | error | short | error | error | error |
| shl/shr | OK | error | error | error | short | error | error | error |
| lnot | error | error | OK→bool | error | short | error | error | error |
| call | error | error | error | error | short | error | error | OK |
| clone | OK | OK | OK | OK(deep) | OK(deep) | OK | OK(shallow) | OK(shallow) |
| dispose | OK | OK | OK | OK(free str) | OK(free str) | OK | no-op | OK(free func) |
| display | OK | OK | OK | OK | OK | OK | OK | OK |
