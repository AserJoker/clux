#include "vm/type_float.h"
#include "vm/value.h"
#include "vm/vm.h"
#include "core/panic.h"

#include <stdio.h>
#include <math.h>

static value_t float_add(vm_t *vm, value_t a, value_t b) {
    double rv = value_as(a, double) + value_as(b, double);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t float_sub(vm_t *vm, value_t a, value_t b) {
    double rv = value_as(a, double) - value_as(b, double);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t float_mul(vm_t *vm, value_t a, value_t b) {
    double rv = value_as(a, double) * value_as(b, double);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t float_div(vm_t *vm, value_t a, value_t b) {
    double bv = value_as(b, double);
    if (bv == 0.0) panic("division by zero");
    double rv = value_as(a, double) / bv;
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t float_mod(vm_t *vm, value_t a, value_t b) {
    double rv = fmod(value_as(a, double), value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t float_neg(vm_t *vm, value_t a) {
    double rv = -value_as(a, double);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t float_eq(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, double) == value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t float_ne(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, double) != value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t float_lt(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, double) < value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t float_le(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, double) <= value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t float_gt(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, double) > value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t float_ge(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, double) >= value_as(b, double));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static void float_display(vm_t *vm, const value_t *v) {
    (void)vm;
    printf("%.17g", value_as(*v, double));
}

const vtable_t VTABLE_FLOAT = {
    .add = float_add,  .sub = float_sub,
    .mul = float_mul,  .div = float_div,
    .mod = float_mod,  .neg = float_neg,
    .eq  = float_eq,   .ne  = float_ne,
    .lt  = float_lt,   .le  = float_le,
    .gt  = float_gt,   .ge  = float_ge,
    .display = float_display,
};
