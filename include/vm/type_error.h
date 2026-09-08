#ifndef _H_CLUX_VM_TYPE_ERROR_
#define _H_CLUX_VM_TYPE_ERROR_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/vtable.h"
#include "core/string.h"

/** error 类型 vtable（仅 dispose/clone，不参与任何运算） */
extern const vtable_t VTABLE_ERROR;

/**
 * error_data_t: error value 的 data 布局
 *
 * - message: 错误消息（必填）
 * - location: 位置信息（可选，NULL 表示无位置，由 AST-walking 层填充）
 *
 * error_data_t 内联在 value 的 data 块中（和 int64_t 一样直接存结构体）。
 */
typedef struct {
    string_t *message;
    string_t *location;
} error_data_t;

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_ERROR_ */
