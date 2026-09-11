#ifndef _H_CLUX_VM_BCODE_DISASM_
#define _H_CLUX_VM_BCODE_DISASM_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/bcode.h"

/**
 * 反汇编字节码模块为字符格式的汇编文本（`.cxs`）。
 *
 * 产物为纯指令序列（与 build --emit-asm 输出格式一致）：
 *   <MNEMONIC> <操作数...>
 *
 * 字符串表是编译器内部概念，**不在文本中暴露**：凡引用字符串的指令
 * （PUSH/STORE/LOAD/PUSH_STRING/DEFINE）其操作数直接内联为转义字符串字面量
 * `PUSH_STRING "hello"`。汇编器在加载时自动把字面量 intern 回字符串表。
 *
 * 该函数为只读消费者：仅读取 bytecode_t 的 strtable 与 code 流，
 * 不依赖 VM 执行，可独立测试。
 *
 * @param bc       字节码模块（须非 NULL，且生命周期覆盖本调用）
 * @param out_path 输出文件路径（写入 UTF-8 文本）；若为 NULL 或打开失败返回 -1
 * @return 0 成功，-1 失败（文件无法打开等）
 */
int bcode_disasm(const bytecode_t *bc, const char *out_path);

/**
 * 反汇编到内存：返回由 `alloc` 管理的 NUL 结尾字符串（与 bcode_disasm 同格式）。
 *
 * 返回的缓冲在 `alloc` 存活期间有效，**调用方不可手动释放**（随
 * delete_allocator 一并回收）。`out_len` 可选，用于取回不含 NUL 的长度。
 * 失败（bc 或 alloc 为 NULL）返回 NULL。
 */
char *bcode_disasm_mem(allocator_t *alloc, const bytecode_t *bc, size_t *out_len);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_BCODE_DISASM_ */
