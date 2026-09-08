#ifndef _H_CLUX_VM_TYPE_INT_
#define _H_CLUX_VM_TYPE_INT_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/vtable.h"

/** 整数类型共用 vtable（i8/i16/i32/i64/u8/u16/u32/u64） */
extern const vtable_t VTABLE_INT;

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_INT_ */
