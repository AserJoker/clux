#include "vm/vm.h"
#include "vm/type.h"
#include "vm/value.h"
#include "core/panic.h"
#include "core/vec.h"

extern void vm_init_builtins(vm_t *vm);

static class_t g_vm_class = {
    .name       = "clux.vm",
    .size       = sizeof(vm_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

/* ---- 基本类型以 type value 注册进 global scope（LOAD "i32" 按名查） ---- */

#include <stddef.h> /* offsetof */

typedef struct {
    const char *name;
    size_t      slot_off; /* vm_t 中对应 type_t* 字段的偏移（编译期常量） */
} builtin_type_entry_t;

static void vm_register_builtin_types(vm_t *vm) {
    static const builtin_type_entry_t entries[] = {
        { "i8",   offsetof(vm_t, type_i8)   }, { "i16",  offsetof(vm_t, type_i16)  },
        { "i32",  offsetof(vm_t, type_i32)  }, { "i64",  offsetof(vm_t, type_i64)  },
        { "u8",   offsetof(vm_t, type_u8)   }, { "u16",  offsetof(vm_t, type_u16)  },
        { "u32",  offsetof(vm_t, type_u32)  }, { "u64",  offsetof(vm_t, type_u64)  },
        { "f32",  offsetof(vm_t, type_f32)  }, { "f64",  offsetof(vm_t, type_f64)  },
        { "bool", offsetof(vm_t, type_bool) }, { "str",  offsetof(vm_t, type_str)  },
        { "void", offsetof(vm_t, type_void) }, { "type", offsetof(vm_t, type_type) },
        { "func", offsetof(vm_t, type_func) },
    };
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        const type_t *t = *(type_t **)((char *)vm + entries[i].slot_off);
        void *data = value_alloc_data_copy(vm->alloc, vm->type_type, &t);
        value_t *tv = value_make_untracked(vm->alloc, vm->type_type, data);
        value_t *stored = scope_define(vm, vm->global_scope, entries[i].name, tv);
        if (!stored || value_is_error(vm, stored)) {
            panic("vm: failed to register builtin type '%s'", entries[i].name);
        }
        value_dispose(vm, tv);
        allocator_free(vm->alloc, (void **)&tv);
    }
}

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

    /* 基本类型注册进 global scope（LOAD 指令按名查 type value） */
    vm_register_builtin_types(vm);

    /* 执行器操作数栈：借用引用，不拥有 value */
    vm->stack = vec_new(alloc, /*owns_element=*/false);

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

    /* 执行器操作数栈（借用引用，不拥有，仅释放向量结构） */
    vec_free(vm->alloc, &vm->stack);

    /* 函数签名类型池：单遍释放（M1 签名只引用内置静态类型，无相互依赖） */
    if (vm->sig_types) {
        size_t n = vec_len(vm->sig_types);
        for (size_t i = 0; i < n; i++) {
            func_type_t *ft = (func_type_t *)vec_get(vm->sig_types, i);
            if (!ft) continue;
            if (ft->sig.params) allocator_free(vm->alloc, (void **)&ft->sig.params);
            if (ft->base.name.ptr) {
                char *np = (char *)ft->base.name.ptr;
                allocator_free(vm->alloc, (void **)&np);
            }
            allocator_free(vm->alloc, (void **)&ft);
        }
        vec_free(vm->alloc, &vm->sig_types);
    }

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
