#include "vm/function.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "core/panic.h"

#include <string.h>

static class_t g_func_class = {
    .name       = "clux.vm.func",
    .size       = sizeof(func_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

/* ---- 生命周期 ---- */

func_t *func_new(allocator_t *alloc,
                  cfunc_t cfunc,
                  scope_t *closure_scope,
                  scope_t *root_scope,
                  strslice_t name) {
    func_t *fn = (func_t *)allocator_new(alloc, &g_func_class, 1);
    if (!fn) panic("vm: out of memory allocating func");
    memset(fn, 0, sizeof(func_t));
    fn->cfunc         = cfunc;
    fn->closure_scope = closure_scope;
    fn->root_scope    = root_scope;
    fn->name          = name;
    return fn;
}

void func_destroy(allocator_t *alloc, func_t **pfn) {
    if (!pfn || !*pfn) return;
    func_t *fn = *pfn;
    if (fn->params) {
        allocator_free(alloc, (void **)&fn->params);
    }
    allocator_free(alloc, (void **)pfn);
}

void func_set_return_type(func_t *fn, const type_t *type) {
    if (!fn) return;
    fn->return_type = type;
}

/* ---- value 构造 ---- */

value_t *func_make_value(vm_t *vm, func_t *fn) {
    void *data = value_alloc_data_copy(vm->alloc, vm->type_func, &fn);
    return value_make(vm, vm->type_func, data);
}
