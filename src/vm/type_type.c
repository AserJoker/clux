#include "vm/type_type.h"
#include "vm/value.h"
#include "vm/vm.h"

/* ---- clone：平凡指针拷贝（type 是单例，data 存的是指向 type_t 的指针） ---- */

static value_t *type_clone(vm_t *vm, value_t *v) {
    void *data = value_alloc_data_copy(vm->alloc, value_type(v), value_data(v));
    return value_make(vm, value_type(v), data);
}

const vtable_t VTABLE_TYPE = {
    .clone = type_clone,
};
