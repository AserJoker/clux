#include "vm/type_type.h"
#include "vm/value.h"

#include <stdio.h>

static void type_display(vm_t *vm, const value_t *v) {
    (void)vm;
    const type_t *t = *(const type_t **)v->data;
    printf("<type %.*s>", (int)t->name.len, t->name.ptr);
}

const vtable_t VTABLE_TYPE = {
    .display = type_display,
};
