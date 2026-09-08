#include "vm/vm.h"
#include "core/panic.h"

extern void vm_init_builtins(vm_t *vm);

static class_t g_vm_class = {
    .name       = "clux.vm",
    .size       = sizeof(vm_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

vm_t *vm_new(allocator_t *alloc) {
    if (!alloc) return NULL;

    vm_t *vm = (vm_t *)allocator_new(alloc, &g_vm_class, 1);
    if (!vm) panic("vm: out of memory allocating vm");

    vm->alloc = alloc;

    vm_init_builtins(vm);

    /* global_scope -> root_scope(module) -> current_scope */
    vm->global_scope  = scope_new(alloc, NULL);
    vm->root_scope    = scope_new(alloc, vm->global_scope);
    vm->current_scope = vm->root_scope;

    return vm;
}

void vm_destroy(vm_t **pvm) {
    if (!pvm || !*pvm) return;
    vm_t *vm = *pvm;

    /* 销毁作用域链：root_scope 是 global 的子作用域 */
    /* 先销毁 root_scope 以下的所有作用域（current_scope 可能在 root 之下） */
    /* 逐层 pop 直到 root_scope，再销毁 root_scope，再销毁 global_scope */
    while (vm->current_scope && vm->current_scope != vm->root_scope) {
        vm_pop_scope(vm);
    }
    scope_destroy(vm, &vm->root_scope);
    scope_destroy(vm, &vm->global_scope);

    vm->current_scope = NULL;

    allocator_free(vm->alloc, (void **)pvm);
}

void vm_push_scope(vm_t *vm) {
    if (!vm) return;
    scope_t *child = scope_new(vm->alloc, vm->current_scope);
    vm->current_scope = child;
}

void vm_pop_scope(vm_t *vm) {
    if (!vm || !vm->current_scope) return;
    if (vm->current_scope == vm->global_scope) {
        panic("vm: cannot pop the global scope");
    }
    scope_t *parent = scope_parent(vm->current_scope);
    scope_destroy(vm, &vm->current_scope);
    vm->current_scope = parent;
}
