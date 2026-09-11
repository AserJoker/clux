#ifndef _H_CLUX_DRIVER_DRIVER_
#define _H_CLUX_DRIVER_DRIVER_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"

/* token_t / vec_t are opaque at this boundary; only pointers cross it. */
typedef struct _token_t token_t;
typedef struct _vec_t vec_t;

/* ===========================================================================
 * clux 编译流水线编排层（Driver）
 *
 * 目前落地的阶段：
 *   ① 加载源文件  —— 读入 allocator 管理的内存缓冲
 *   ② 词法分析    —— Lexer 切分为 token 并汇入 token 池（vec<token_t*>）
 *   ③ 语法分析    —— Parser 将 token 流解析为 AST
 *   ④ 语义分析    —— Sema 构建作用域树 + shadow VM 类型检查（快速失败）
 *   ⑤ 输出 AST    —— 打印 AST 结构到 stdout（M1 阶段）
 *
 * 未来阶段（Bytecode Compiler / Bytecode VM）将在此框架内顺序插入。
 * =========================================================================== */

/**
 * 加载源码文件到内存（allocator 管理）。
 *
 * 以二进制模式（"rb"）整读文件。返回的 `out_data` 指向一块由 allocator
 * 分配、长度为 `out_len` 的缓冲，其生命周期由 `alloc` 管理——调用方**不**
 * 应手动释放它；只要 `alloc` 仍存活（且未被 delete），`out_data` 即可安全
 * 读取。
 *
 * 返回 0 成功；返回 -1 表示文件无法打开或读取失败（此时 `*out_data` 为 NULL）。
 */
int driver_load_source(allocator_t *alloc,
                       const char *path,
                       const char **out_data,
                       size_t *out_len);

/**
 * 加载源码并做词法分析，结果汇入 token 池。
 *
 * `out_pool` 接收一个新分配的 `vec_t*`（元素为 `token_t*`，owns_element=true，
 * 随 `vec_free` 自动释放每个 token）。池中按源码顺序保存所有 token，
 * 包括 `TOKEN_TYPE_WHITESPACE` / 注释，直至并包含 `TOKEN_TYPE_EOF`。
 * 词法错误以 `TOKEN_TYPE_ERROR` token 形式留在池中（不 fail-fast）。
 *
 * 返回 0 成功；返回 -1 表示文件无法打开（此时 `*out_pool` 为 NULL）。
 */
int driver_lex_file(allocator_t *alloc, const char *path, vec_t **out_pool);

/**
 * 流水线顶层入口：加载 → 词法分析 → 语法分析 → 语义分析 → 输出 AST。
 *
 * 词法错误直接快速失败（返回 1）。词法通过后进入语法分析；
 * 语法错误时输出诊断到 stderr（返回 1）。语法通过后进入语义分析；
 * 语义错误时输出诊断到 stderr（返回 1，不输出 AST）。
 *
 * 返回进程退出码：
 *   0  —— 成功（无词法/语法/语义错误、文件可读）
 *   1  —— 编译错误（文件无法打开 / 词法错误 / 语法错误 / 语义错误）
 */
int driver_run_file(const char *path);

/**
 * 编译流水线（加载 → 词法 → 语法 → 语义 → 编译字节码）后停止，**不执行**
 * 程序，仅将产物 `bytecode_t` 反汇编为字符格式汇编并写出到 `out_path`
 * （`.cxs` 文本）。
 *
 * 返回进程退出码：
 *   0  —— 编译成功并写出 asm
 *   1  —— 编译错误 / 文件无法打开 / asm 文件无法写出
 */
int driver_build_asm(const char *src_path, const char *out_path);

/**
 * 逆向流程：将 `.cxs` 汇编文本汇编为字节码并直接执行（等价于
 * `build --emit-asm` 产物的反过程）。
 *
 * 复用 run 的执行阶段（注册函数 → 调用 main）。与 `driver_run_file`
 * 不同的是这里不经历 lex→parse→sema→compile，而是从已序列化的
 * 字符格式字节码重新加载。
 *
 * 返回进程退出码：
 *   0  —— 汇编 + 执行成功
 *   1  —— 汇编错误 / 文件无法打开 / 执行错误 / 无入口 `main`
 */
int driver_run_asm(const char *asm_path);

#ifdef __cplusplus
}
#endif
#endif
