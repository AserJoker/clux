#include "vm/type_str.h"
#include "vm/value.h"
#include "vm/vm.h"
#include "core/string.h"
#include "core/panic.h"

#include <stdio.h>

static value_t str_eq(vm_t *vm, value_t a, value_t b) {
    if (a.type != b.type) {
        bool rv = false;
        void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
        return value_make(vm->type_bool, data);
    }
    string_t *sa = *(string_t **)a.data;
    string_t *sb = *(string_t **)b.data;
    bool rv = string_equals(sa, sb);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static value_t str_ne(vm_t *vm, value_t a, value_t b) {
    if (a.type != b.type) {
        bool rv = true;
        void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
        return value_make(vm->type_bool, data);
    }
    string_t *sa = *(string_t **)a.data;
    string_t *sb = *(string_t **)b.data;
    bool rv = !string_equals(sa, sb);
    void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &rv);
    return value_make(vm->type_bool, data);
}

static void str_dispose(vm_t *vm, value_t *v) {
    (void)vm;
    string_t **sp = (string_t **)v->data;
    if (sp && *sp) {
        string_free(sp);
    }
}

static value_t str_clone(vm_t *vm, value_t v) {
    string_t *src = *(string_t **)v.data;
    string_t *copy = string_from_string(vm->alloc, src);
    if (!copy) panic("vm: out of memory cloning string");
    void *data = value_alloc_data_copy(vm->alloc, v.type, &copy);
    return value_make(v.type, data);
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
