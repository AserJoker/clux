#include "vm/type_int.h"
#include "vm/value.h"
#include "vm/vm.h"
#include "core/panic.h"

#include <stdio.h>
#include <inttypes.h>

/* ---- 整数类型共用逻辑 ---- */

static value_t int_add(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) + value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_sub(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) - value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_mul(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) * value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_div(vm_t *vm, value_t a, value_t b) {
    int64_t bv = value_as(b, int64_t);
    if (bv == 0) panic("division by zero");
    int64_t rv = value_as(a, int64_t) / bv;
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_mod(vm_t *vm, value_t a, value_t b) {
    int64_t bv = value_as(b, int64_t);
    if (bv == 0) panic("modulo by zero");
    int64_t rv = value_as(a, int64_t) % bv;
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_neg(vm_t *vm, value_t a) {
    int64_t rv = -value_as(a, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_eq(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) == value_as(b, int64_t));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t int_ne(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) != value_as(b, int64_t));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t int_lt(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) < value_as(b, int64_t));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t int_le(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) <= value_as(b, int64_t));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t int_gt(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) > value_as(b, int64_t));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t int_ge(vm_t *vm, value_t a, value_t b) {
    bool rv = (value_as(a, int64_t) >= value_as(b, int64_t));
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t int_band(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) & value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_bor(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) | value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_bxor(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) ^ value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_bnot(vm_t *vm, value_t a) {
    int64_t rv = ~value_as(a, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_shl(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) << value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static value_t int_shr(vm_t *vm, value_t a, value_t b) {
    int64_t rv = value_as(a, int64_t) >> value_as(b, int64_t);
    void *data = value_alloc_data_copy(vm->alloc, a.type, &rv);
    return value_make(a.type, data);
}

static void int_display(vm_t *vm, const value_t *v) {
    (void)vm;
    printf("%" PRId64, value_as(*v, int64_t));
}

const vtable_t VTABLE_INT = {
    .add = int_add,  .sub = int_sub,
    .mul = int_mul,  .div = int_div,
    .mod = int_mod,  .neg = int_neg,
    .eq  = int_eq,   .ne  = int_ne,
    .lt  = int_lt,   .le  = int_le,
    .gt  = int_gt,   .ge  = int_ge,
    .band = int_band, .bor = int_bor,
    .bxor = int_bxor, .bnot = int_bnot,
    .shl = int_shl,   .shr = int_shr,
    .display = int_display,
};
