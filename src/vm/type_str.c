#include "vm/type_str.h"
#include "vm/value_internal.h"
#include "vm/vm.h"
#include "core/string.h"
#include "core/panic.h"

#include <stdio.h>

static value_t *bool_store(vm_t *vm, bool val) {
    void *data = value_alloc_data(vm->alloc, vm->type_bool);
    *(bool *)data = val;
    return value_make(vm, vm->type_bool, data);
}

static value_t *str_eq(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, eq, "==");
    if (a->type != vm->type_str || b->type != vm->type_str)
        return value_make_error(vm, "==: type mismatch");
    string_t *sa = *(string_t **)a->data;
    string_t *sb = *(string_t **)b->data;
    return bool_store(vm, string_equals(sa, sb));
}

static value_t *str_ne(vm_t *vm, value_t *a, value_t *b) {
    VTABLE_BINARY(vm, a, b, ne, "!=");
    if (a->type != vm->type_str || b->type != vm->type_str)
        return value_make_error(vm, "!=: type mismatch");
    string_t *sa = *(string_t **)a->data;
    string_t *sb = *(string_t **)b->data;
    return bool_store(vm, !string_equals(sa, sb));
}

static void str_dispose(vm_t *vm, value_t *v) {
    (void)vm;
    string_t **sp = (string_t **)v->data;
    if (sp && *sp) {
        string_free(sp);
    }
}

static value_t *str_clone(vm_t *vm, value_t *v) {
    string_t *src = *(string_t **)v->data;
    string_t *copy = string_from_string(vm->alloc, src);
    if (!copy) panic("vm: out of memory cloning string");
    void *data = value_alloc_data_copy(vm->alloc, v->type, &copy);
    return value_make(vm, v->type, data);
}

static void str_display(vm_t *vm, const value_t *v) {
    (void)vm;
    string_t *s = *(string_t **)v->data;
    printf("%s", string_cstr(s));
}

const vtable_t VTABLE_STR = {
    .eq = str_eq, .ne = str_ne,
    .dispose = str_dispose, .clone = str_clone,
    .display = str_display,
};
