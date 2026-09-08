#ifndef _H_CLUX_VM_TYPE_INT_
#define _H_CLUX_VM_TYPE_INT_
#ifdef __cplusplus
extern "C" {
#endif

#include "vm/vtable.h"

/** 有符号整数 vtable（i8/i16/i32/i64） */
extern const vtable_t VTABLE_INT_SIGNED;

/** 无符号整数 vtable（u8/u16/u32/u64） */
extern const vtable_t VTABLE_INT_UNSIGNED;

#ifdef __cplusplus
}
#endif
#endif /* _H_CLUX_VM_TYPE_INT_ */
