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
 *   ③ 输出单词表  —— 打印 token 表到 stdout（由 driver_run_file 直接完成）
 *
 * 未来阶段（Parser / Sema / Interp）将在此框架内顺序插入。
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
 * 流水线顶层入口：加载 → 词法分析 → 输出单词表。
 *
 * 将 token 表逐行打印到 stdout（每个 token 一行，跳过 EOF），位置范围与文本
 * 转义见实现约定；`TOKEN_TYPE_ERROR` 的诊断打印到 stderr。
 *
 * 返回进程退出码：
 *   0  —— 成功（无词法错误、文件可读）
 *   1  —— 编译错误（文件无法打开 / 存在词法错误）
 */
int driver_run_file(const char *path);

#ifdef __cplusplus
}
#endif
#endif
