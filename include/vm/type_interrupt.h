#ifndef _H_CLUX_VM_TYPE_INTERRUPT_
#define _H_CLUX_VM_TYPE_INTERRUPT_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/vtable.h"
#include <stddef.h>

/** interrupt 类型 vtable（引擎级控制流哨兵，不参与任何运算） */
extern const vtable_t VTABLE_INTERRUPT;

/**
 * interrupt_kind_t: 引擎级控制流信号种类
 *
 * interrupt 是特殊 value（与 error 同级），不由普通指令回调消费，
 * 由执行回调/执行器统一出口识别并分派。目前只有 RETURN，
 * 未来可扩展 BREAK / CONTINUE。
 */
typedef enum {
    INTERRUPT_RETURN = 0,
    /* 未来: INTERRUPT_BREAK, INTERRUPT_CONTINUE */
} interrupt_kind_t;

/**
 * interrupt_data_t: interrupt value 的 data 布局
 *
 * 内联在 value 的 data 块中（和 error_data_t 同模式）。
 */
typedef struct {
    interrupt_kind_t kind;
} interrupt_data_t;

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_INTERRUPT_ */
