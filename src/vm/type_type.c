#include "vm/type_type.h"
#include "vm/value.h"
#include "vm/vm.h"

/* ---- clone：平凡指针拷贝（type 是单例，data 存的是指向 type_t 的指针） ---- */

static value_t *type_clone(vm_t *vm, value_t *v) {
    void *data = value_alloc_data_copy(vm->alloc, value_type(v), value_data(v));
    return value_make(vm, value_type(v), data);
}

/* ---- type value 的 == / extends（鸭子类型判断，代理到类型自身） ---- */

static value_t *type_value_eq(vm_t *vm, value_t *a, value_t *b) {
    return value_type_eq(vm, a, b);
}

static value_t *type_value_ne(vm_t *vm, value_t *a, value_t *b) {
    if (value_is_error(vm, a)) return a;
    if (value_is_error(vm, b)) return b;
    if (value_type(a) != vm->type_type || value_type(b) != vm->type_type)
        return value_make_error(vm, "!=: type value required");
    const type_t *ta = value_as(a, const type_t *);
    const type_t *tb = value_as(b, const type_t *);
    bool r = !type_equal(vm, ta, tb);
    if (value_is_shadow(a) || value_is_shadow(b))
        return value_make_shadow(vm, vm->type_bool);
    void *data = value_alloc_data(vm->alloc, vm->type_bool);
    *(bool *)data = r;
    return value_make(vm, vm->type_bool, data);
}

static value_t *type_value_extends(vm_t *vm, value_t *a, value_t *b) {
    return value_type_extends(vm, a, b);
}

const vtable_t VTABLE_TYPE = {
    .eq = type_value_eq,
    .ne = type_value_ne,
    .clone = type_clone,
    .extends = type_value_extends,
};
