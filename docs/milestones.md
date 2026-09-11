# clux 里程碑规划

## M1: 最小可执行子集 ✅ 已完成

**目标**：实现"裁剪后的 C"子集的解释器

**语言特性**：基础类型(i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/void)、var 变量声明(含推断)、func 函数定义(a:type 风格)、if/else、while、for、break/continue/return、算术/比较/逻辑/位/赋值运算、true/false 字面量、printf 硬编码、C 风格注释

**不支持**：指针、所有权、struct/union/enum、数组、字符串、模块、错误处理、预编译指令

**交付标准**：`clux run hello.cx` 能正确执行含变量、运算、控制流、函数调用的程序 ✅ 8 个 examples 端到端通过、764 单元测试全过

**任务**：T1 骨架 → T2 Lexer → T3 AST → T4 Parser → T5 类型系统 → T6 语义分析 → T7 解释器 → T8 端到端集成 ✅ 全部完成

---

---

## M2: 与 C 表达力齐平（指针除外）🔨 设计完成，待实施

**目标**：补全语言功能达到 C 的表达力（指针除外），保持 clux 自己的语法风格

**语言特性**：struct、enum（严格分离）、静态数组 `[N]T`、元组 `<T1,T2>`、switch（if 语法糖）、do-while、type 别名 + 类型计算、sizeof/alignof/typeof、函数类型、位运算复合赋值、三元表达式、元组↔数组互转、鸭子类型协议

**不支持**：tagged union / cunion（移至后续里程碑）、slice、goto、逗号运算符、指针

**交付标准**：全部单元测试通过 + 8+ 个新 examples 端到端通过

**设计文档**：[m2-design.md](m2-design.md)

**任务**：
- Phase 0: Lexer + AST 基础（新关键字/符号 + 新 AST 节点 + `parse_type_expr`）
- Phase 2: VM 类型系统扩展（复合类型 + interning + vtable + `is_own` 借用字段）
- Phase 3: Sema 扩展（`resolve_type` 升级 + 类型计算 + 鸭子类型检查）
- Phase 4: 构造 + 访问（`.<type>{...}` + 字段访问 + enum variant + 新字节码）
- Phase 5: 控制流 + 表达式补全（switch desugar + do-while + 三元 + 位运算复合赋值）
- Phase 6: 集成 + 测试（examples + 全测试通过 + 文档更新）
- 工具链: 字节码可执行产物（`.cxs` 文本 / `.cxb` 二进制），编译 `build --emit-asm` / `--emit-bin`，互转 `build --to-bin` / `--to-asm`（内容嗅探，不依赖扩展名，`--input=` 可强制），执行 `run --asm` / `--bin` ✅ 已完成（2026-09-12）

---

## 后续里程碑（待详细设计）

- **M3**: 指针与所有权系统 Phase 1
- **M4**: 切片与字符串（静态数组已提前到 M2）
- **M5**: FFI 系统
- **M6**: 模块系统
- **M7**: 转译 C 后端
- **M8**: 所有权系统 Phase 2（借用与生命周期）
- **M9**: 错误处理机制
- **M10**: 标准库 v1
- **M11**: tagged union / cunion（鸭子类型协议）
- **M12**: 原生编译后端（远期）
- **M13**: 自举（远期）
