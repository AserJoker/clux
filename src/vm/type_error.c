#include "vm/type_error.h"
#include "vm/value.h"
#include "vm/vm.h"
#include "core/panic.h"

#include <stdio.h>

/* error value 的 data 布局: error_data_t* （定义在 type_error.h 中） */

/* ---- dispose ---- */

static void error_dispose(vm_t *vm, value_t *v) {
    (void)vm;
    error_data_t *ed = *(error_data_t **)v->data;
    if (ed) {
        if (ed->message)  string_free(&ed->message);
        if (ed->location) string_free(&ed->location);
        /* ed 本身通过 allocator_free 释放 data 块时被释放 */
    }
}

/* ---- clone ---- */

static value_t error_clone(vm_t *vm, value_t v) {
    error_data_t *src = *(error_data_t **)v.data;

    error_data_t *ed = (error_data_t *)value_alloc_data(vm->alloc, v.type);
    ed->message  = src->message  ? string_from_string(vm->alloc, src->message)  : NULL;
    ed->location = src->location ? string_from_string(vm->alloc, src->location) : NULL;
    if (src->message && !ed->message)  panic("vm: out of memory cloning error message");
    if (src->location && !ed->location) panic("vm: out of memory cloning error location");

    void *data = value_alloc_data_copy(vm->alloc, v.type, &ed);
    return value_make(v.type, data);
}

/* ---- display ---- */

static void error_display(vm_t *vm, const value_t *v) {
    (void)vm;
    error_data_t *ed = *(error_data_t **)v->data;
    if (ed && ed->message) {
        printf("error: %s", string_cstr(ed->message));
    } else {
        printf("error");
    }
}

const vtable_t VTABLE_ERROR = {
    .dispose = error_dispose,
    .clone   = error_clone,
    .display = error_display,
};
