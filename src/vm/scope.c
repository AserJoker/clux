#include "vm/scope.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "core/strmap.h"
#include "core/panic.h"

#include <string.h>

static class_t g_scope_class = {
    .name       = "clux.vm.scope",
    .size       = sizeof(scope_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

scope_t *scope_new(allocator_t *alloc, scope_t *parent) {
    scope_t *s = (scope_t *)allocator_new(alloc, &g_scope_class, 1);
    if (!s) panic("vm: out of memory allocating scope");

    s->parent   = parent;
    s->alloc    = alloc;
    s->vars     = strmap_new(alloc, /*owns_value=*/false);
    s->owned    = vec_new(alloc, /*owns_element=*/false);
    s->children = vec_new(alloc, /*owns_element=*/false);
    if (!s->vars)     panic("vm: out of memory allocating scope vars");
    if (!s->owned)    panic("vm: out of memory allocating scope owned");
    if (!s->children) panic("vm: out of memory allocating scope children");

    /* 注册到父作用域的 children */
    if (parent && parent->children) {
        vec_push(parent->children, alloc, s);
    }

    return s;
}

/* ---- 内部：从父作用域的 children 中移除自身 ---- */

static void scope_remove_from_parent(scope_t *scope) {
    if (!scope || !scope->parent || !scope->parent->children) return;
    size_t n = vec_len(scope->parent->children);
    for (size_t i = 0; i < n; i++) {
        if (vec_get(scope->parent->children, i) == scope) {
            vec_swap_remove(scope->parent->children, i);
            break;
        }
    }
}

void scope_destroy(vm_t *vm, scope_t **pscope) {
    if (!pscope || !*pscope) return;
    scope_t *scope = *pscope;

    /* 0. 从父作用域的 children 中移除自身 */
    scope_remove_from_parent(scope);

    /* 1. dispose + free 所有 owned value（scope 唯一的生命周期管理入口） */
    size_t owned_n = vec_len(scope->owned);
    for (size_t i = 0; i < owned_n; i++) {
        value_t *v = (value_t *)vec_get(scope->owned, i);
        if (v) {
            value_dispose(vm, v);  /* 释放 data */
            allocator_free(scope->alloc, (void **)&v);  /* 释放 value_t 结构体 */
        }
    }
    vec_free(scope->alloc, &scope->owned);

    /* 2. strmap_free 仅释放 key 副本（owns_value=false，不释放 value_t*） */
    strmap_free(scope->alloc, &scope->vars);

    /* 3. 释放 children 向量（不拥有元素，仅释放向量结构） */
    vec_free(scope->alloc, &scope->children);

    /* 4. 释放 scope 结构体 */
    allocator_free(scope->alloc, (void **)pscope);
}

void scope_destroy_subtree(vm_t *vm, scope_t **pscope) {
    if (!pscope || !*pscope) return;
    scope_t *scope = *pscope;

    /* 1. 递归销毁所有子作用域（子作用域会从本 scope 的 children 中移除自身） */
    while (!vec_is_empty(scope->children)) {
        scope_t *child = (scope_t *)vec_last(scope->children);
        scope_destroy_subtree(vm, &child);
    }

    /* 2. 销毁自身（从父作用域的 children 中移除 + dispose vars + free） */
    scope_destroy(vm, pscope);
}

void scope_track(vm_t *vm, scope_t *scope, value_t *v) {
    (void)vm;
    if (!scope || !v || !value_type(v)) return;
    vec_push(scope->owned, scope->alloc, v);
}

value_t *scope_define(vm_t *vm, scope_t *scope, const char *name, value_t *v) {
    if (!scope || !name) return NULL;

    /* 临时切换 current_scope 到目标 scope，clone 后 value 自动注册到 owned */
    scope_t *saved = vm->current_scope;
    vm->current_scope = scope;
    value_t *cloned = value_clone(vm, v);
    vm->current_scope = saved;

    /* 插入 vars 借用映射；若 name 已存在，旧映射被替换 */
    /* 旧 value_t* 仍在 owned 中，由 scope_destroy 统一 dispose */
    strmap_insert(scope->vars, vm->alloc, name, cloned);

    return cloned;
}

value_t *scope_lookup(const scope_t *scope, strslice_t name) {
    for (const scope_t *s = scope; s; s = s->parent) {
        char buf[256];
        if (name.len < sizeof(buf)) {
            memcpy(buf, name.ptr, name.len);
            buf[name.len] = '\0';
            value_t *v = (value_t *)strmap_get(s->vars, buf);
            if (v) return v;
        }
    }
    return NULL;
}

bool scope_assign(vm_t *vm, scope_t *scope, strslice_t name, value_t *v) {
    for (scope_t *s = scope; s; s = s->parent) {
        char buf[256];
        if (name.len < sizeof(buf)) {
            memcpy(buf, name.ptr, name.len);
            buf[name.len] = '\0';
            value_t *old = (value_t *)strmap_get(s->vars, buf);
            if (old) {
                /* clone 新值到 owned，更新 vars 指向新值 */
                /* 旧值留在 owned 中，由 scope_destroy 统一 dispose */
                scope_t *saved = vm->current_scope;
                vm->current_scope = s;
                value_t *new_val = value_clone(vm, v);
                vm->current_scope = saved;

                strmap_insert(s->vars, vm->alloc, buf, new_val);
                return true;
            }
        }
    }
    return false;
}
