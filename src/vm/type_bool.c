#include "vm/type_bool.h"
#include "vm/value.h"
#include "vm/vm.h"

#include <stdio.h>

static value_t bool_eq(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, bool) == value_as(b, bool));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t bool_ne(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, bool) != value_as(b, bool));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t bool_lnot(vm_t *vm, value_t a) {
    bool rv = !value_as(a, bool);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static void bool_display(vm_t *vm, const value_t *v) {
    (void)vm;
    printf("%s", value_as(*v, bool) ? "true" : "false");
}

const vtable_t VTABLE_BOOL = {
    .eq = bool_eq, .ne = bool_ne,
    .lnot = bool_lnot,
    .display = bool_display,
};
