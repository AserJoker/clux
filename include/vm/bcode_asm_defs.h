#ifndef _H_CLUX_VM_BCODE_ASM_DEFS_
#define _H_CLUX_VM_BCODE_ASM_DEFS_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/bcode.h"

/* ================================================================ */
/* 汇编/反汇编共享操作数类型 + 解码表（单一事实源）                   */
/* ================================================================ */

/* 操作数宽度分类：反汇编器据此从 code 流读取、汇编器据此向 code 流写入。
 * 索引与 bcode_op_t 一一对应（见下方 BCODE_ASM_TABLE）。 */
typedef enum {
    BCODE_ASM_OP_NONE,
    BCODE_ASM_OP_STR,     /* 字符串字面量：汇编文本内联 "..."，内部仍写 strtable 索引 */
    BCODE_ASM_OP_U32,
    BCODE_ASM_OP_I8,
    BCODE_ASM_OP_I16,
    BCODE_ASM_OP_I32,
    BCODE_ASM_OP_I64,
    BCODE_ASM_OP_U8,
    BCODE_ASM_OP_U16,
    BCODE_ASM_OP_U64,
    BCODE_ASM_OP_F32,
    BCODE_ASM_OP_F64,
    BCODE_ASM_OP_BOOL,
} bcode_asm_operand_t;

typedef struct {
    const char            *mnemonic;      /* 全大写助记符（反汇编输出 & 汇编识别） */
    bcode_asm_operand_t    operands[4];   /* 按出现顺序，BCODE_ASM_OP_NONE 结束 */
} bcode_asm_entry_t;

/* 按 bcode_op_t 枚举值索引。反汇编与汇编共用，新增/改名 opcode 只改此表。 */
extern const bcode_asm_entry_t BCODE_ASM_TABLE[];
extern const size_t           BCODE_ASM_TABLE_COUNT;

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_BCODE_ASM_DEFS_ */
