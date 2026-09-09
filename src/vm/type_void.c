#include "vm/type_void.h"
#include "vm/value.h"

/* void 无 data 载荷，clone 平凡重建（undefined 可被 scope 持有/clone） */
static value_t *void_clone(vm_t *vm, value_t *v) {
    (void)v;
    return value_make(vm, value_type(v), NULL);
}

const vtable_t VTABLE_VOID = {
    .clone = void_clone,
};
