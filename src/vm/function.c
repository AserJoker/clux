#include "vm/function.h"
#include "vm/vm.h"
#include "vm/value.h"
#include "core/panic.h"
#include "core/vec.h"

#include <string.h>

static class_t g_func_class = {
    .name       = "clux.vm.func",
    .size       = sizeof(func_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

/* ---- 生命周期 ---- */

value_t *func_new(vm_t *vm,
                  cfunc_t cfunc,
                  scope_t *closure_scope,
                  scope_t *root_scope,
                  const type_t *sig_type,
                  strslice_t name) {
    if (!vm || !vm->alloc || !sig_type) return NULL;
    func_t *fn = (func_t *)allocator_new(vm->alloc, &g_func_class, 1);
    if (!fn) panic("vm: out of memory allocating func");
    memset(fn, 0, sizeof(func_t));
    fn->cfunc         = cfunc;
    fn->closure_scope = closure_scope;
    fn->root_scope    = root_scope;
    fn->name          = name;

    /* 函数对象注册进 vm->functions（vm 统一释放，value 共享指针不 double free） */
    if (vm->functions) vec_push(vm->functions, vm->alloc, fn);

    /* 包装为 func value：data 存 func_t*，type 即签名类型 */
    void *data = value_alloc_data_copy(vm->alloc, sig_type, &fn);
    return value_make_untracked(vm->alloc, sig_type, data);
}

void func_destroy(allocator_t *alloc, func_t **pfn) {
    if (!pfn || !*pfn) return;
    /* 签名信息归 vm 类型池所有，func_t 不释放 sig_type */
    allocator_free(alloc, (void **)pfn);
}
