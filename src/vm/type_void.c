#include "vm/type_void.h"
#include "vm/value_internal.h"

#include <stdio.h>

static void void_display(vm_t *vm, const value_t *v) {
    (void)vm; (void)v;
    printf("void");
}

const vtable_t VTABLE_VOID = {
    .display = void_display,
};
