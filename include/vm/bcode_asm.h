#ifndef _H_CLUX_VM_BCODE_ASM_
#define _H_CLUX_VM_BCODE_ASM_
#ifdef __cplusplus
extern "C" {
#endif

#include "core/allocator.h"

struct bytecode_t;
typedef struct bytecode_t bytecode_t;

/**
 * 将 `.cxs` 汇编文本解析为 `bytecode_t`（与 build --emit-asm 输出格式互逆）。
 *
 * 文本格式（纯指令序列，无独立字符串段）：
 *   <MNEMONIC> <操作数...>
 * 引用字符串的指令内联转义字符串字面量，例如 `PUSH_STRING "hello"`。
 * 汇编器自动将字面量 intern 进 bytecode_t 内部字符串表，并写入其索引。
 *
 * 所有内存（含 strtable 字符串与 code 缓冲）由 `alloc` 管理；成功时
 * `*out_bc` 指向新分配的模块，调用方负责 bcode_destroy + delete_allocator。
 *
 * @return 0 成功；非 0 失败（解析错误，诊断已打印到 stderr，*out_bc 置 NULL）
 */
int bcode_asm_parse(allocator_t *alloc, const char *text, size_t len,
                    bytecode_t **out_bc);

/**
 * 从文件读取 `.cxs` 文本并解析（内部 fopen/fread 整读）。
 * 语义同 bcode_asm_parse，仅输入来源为文件。
 */
int bcode_asm_from_file(allocator_t *alloc, const char *path, bytecode_t **out_bc);

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_BCODE_ASM_ */
