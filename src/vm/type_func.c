#include "vm/type_func.h"
#include "vm/value.h"
#include "vm/function.h"
#include "vm/vm.h"
#include "vm/scope.h"
#include "vm/type_error.h"
#include "core/panic.h"

/* ---- dispose ---- */

static void func_dispose(vm_t *vm, value_t *v) {
    func_t **fp = (func_t **)value_data(v);
    if (fp && *fp) {
        func_destroy(vm->alloc, fp);
    }
}

/* ---- clone ---- */

static value_t *func_clone(vm_t *vm, value_t *v) {
    /* 函数值不可变，浅拷贝指针即可 */
    const func_t *fn = *(const func_t **)value_data(v);
    void *data = value_alloc_data_copy(vm->alloc, value_type(v), &fn);
    return value_make(vm, value_type(v), data);
}

/* ---- call: 通过 vtable 分派的函数调用 ---- */

static value_t *func_vcall(vm_t *vm, value_t *callee, value_t **args, size_t argc) {
    func_t *fn = *(func_t **)value_data(callee);
    if (!fn || !fn->cfunc) {
        return value_make_error(vm, "func call: invalid function");
    }

    /* 参数数量检查：非 variadic 函数拒绝多余实参 */
    if (!fn->is_variadic && argc > fn->param_count) {
        return value_make_error(vm, "func call: too many arguments");
    }

    /* 1. 保存现场 */
    scope_t *caller_root  = vm->root_scope;
    scope_t *caller_scope = vm->current_scope;

    /* 2. 切换到函数的模块作用域 */
    vm->root_scope = fn->root_scope;

    /* 3. 进入闭包作用域 */
    vm->current_scope = fn->closure_scope;

    /* 4. push 匿名局部作用域 */
    vm_push_scope(vm);

    /* 5. safe_cast + clone 参数到当前作用域（value_clone 自动注册到 owned） */
    value_t *local_args[argc > 0 ? argc : 1];
    value_t *ret = NULL;
    bool is_error = false;

    for (size_t i = 0; i < argc; i++) {
        if (fn->params && i < fn->param_count && fn->params[i]
            && value_type(args[i]) != fn->params[i]) {
            /* safe_cast: implicit_cast（auto-tracked），失败返回 error */
            value_t *casted = value_implicit_cast(vm, args[i], fn->params[i]);
            if (value_is_error(vm, casted)) {
                is_error = true;
                ret = casted;
                break;
            }
            local_args[i] = casted;
        } else {
            /* 类型匹配、无类型声明、或 variadic 额外参数：clone（auto-tracked） */
            local_args[i] = value_clone(vm, args[i]);
        }
    }

    /* 6. call cfunc（仅在参数处理成功时） */
    if (!is_error) {
        value_t *result = fn->cfunc(vm, fn, argc, local_args);
        if (result && value_type(result)) {
            if (value_is_error(vm, result)) {
                is_error = true;
                ret = result;  /* error 借用 callee scope */
            } else if (fn->return_type && value_type(result) != fn->return_type) {
                /* safe_cast 返回值到声明的返回类型（auto-tracked 到 callee scope） */
                ret = value_implicit_cast(vm, result, fn->return_type);
                if (value_is_error(vm, ret)) {
                    is_error = true;
                }
            } else {
                /* 类型匹配或无返回类型声明，借用 */
                ret = result;
            }
        }
    }

    /* 7. clone 返回值/error 到调用方作用域 */
    /* 临时切换 current_scope 到 caller，clone 后自动 track 到 caller 的 owned */
    scope_t *callee_current = vm->current_scope;
    vm->current_scope = caller_scope;
    if (ret && value_type(ret)) {
        ret = value_clone(vm, ret);
    }

    /* 8. 平衡作用域栈：销毁 callee 作用域子树 */
    vm->current_scope = callee_current;
    if (is_error) {
        /* error 路径：砍掉子树，不走 pop_scope（避免触发 defer） */
        while (vm->current_scope != fn->closure_scope) {
            if (!vm->current_scope || vm->current_scope == vm->global_scope) break;
            scope_t *cur = vm->current_scope;
            scope_t *parent = scope_parent(cur);
            scope_destroy_subtree(vm, &cur);
            vm->current_scope = parent;
        }
    } else {
        /* 正常路径：逐层 pop_scope */
        while (vm->current_scope != fn->closure_scope) {
            if (!vm->current_scope || vm->current_scope == vm->global_scope) break;
            vm_pop_scope(vm);
        }
    }

    /* 9. 恢复现场 */
    vm->root_scope    = caller_root;
    vm->current_scope = caller_scope;

    return ret;
}

const vtable_t VTABLE_FUNC = {
    .dispose = func_dispose,
    .clone   = func_clone,
    .call    = func_vcall,
};
