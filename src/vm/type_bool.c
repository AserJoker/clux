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

/* ---- 显式转换：bool → int/float ---- */

static value_t bool_explicit_cast(vm_t *vm, value_t v, const type_t *target) {
    bool bv = value_as(v, bool);

    if (target == vm->type_i8 || target == vm->type_i16 ||
        target == vm->type_i32 || target == vm->type_i64) {
        int64_t iv = bv ? 1 : 0;
        void *data = value_alloc_data_copy(vm->alloc, target, &iv);
        return value_make(target, data);
    }

    if (target == vm->type_u8 || target == vm->type_u16 ||
        target == vm->type_u32 || target == vm->type_u64) {
        uint64_t uv = bv ? 1 : 0;
        void *data = value_alloc_data_copy(vm->alloc, target, &uv);
        return value_make(target, data);
    }

    if (target == vm->type_f32 || target == vm->type_f64) {
        double dv = bv ? 1.0 : 0.0;
        void *data = value_alloc_data_copy(vm->alloc, target, &dv);
        return value_make(target, data);
    }

    return value_make_error(vm, "explicit cast: incompatible target type");
}

const vtable_t VTABLE_BOOL = {
    .eq = bool_eq, .ne = bool_ne,
    .lnot = bool_lnot,
    .display = bool_display,
    .explicit_cast = bool_explicit_cast,
};
