#include "vm/type_interrupt.h"
#include "vm/value.h"
#include "vm/vm.h"
#include "core/panic.h"

/* interrupt value 的 data 布局: interrupt_data_t 内联（kind 平凡值） */

/* ---- clone ---- */

static value_t *interrupt_clone(vm_t *vm, value_t *v) {
    interrupt_data_t *src = (interrupt_data_t *)value_data(v);

    void *data = value_alloc_data_copy(vm->alloc, value_type(v), src);
    return value_make(vm, value_type(v), data);
}

const vtable_t VTABLE_INTERRUPT = {
    .clone = interrupt_clone,
};
