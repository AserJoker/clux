#include "ctfe/ctfe.h"

#include "core/panic.h"
#include "core/string.h"
#include "core/strslice.h"
#include "parser/ast_assign.h"
#include "parser/ast_binary.h"
#include "parser/ast_block.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_call.h"
#include "parser/ast_cast.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_for.h"
#include "parser/ast_func_def.h"
#include "parser/ast_ident.h"
#include "parser/ast_if.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_return.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_unary.h"
#include "parser/ast_var_def.h"
#include "parser/ast_while.h"
#include "parser/lexer.h"
#include "sema/symbol.h"
#include "vm/scope.h"
#include "vm/type.h"
#include "vm/type_error.h"
#include "vm/value.h"

#include <string.h>

/* ===========================================================================
 * 内部工具
 * =========================================================================== */

static value_t *err(ctfe_ctx_t *ctx, const char *msg) {
    return value_make_error(ctx->vm, msg);
}

/* 兄弟链计数 */
static size_t count_siblings(const ast_node_t *node) {
    size_t n = 0;
    for (const ast_node_t *p = node; p; p = p->next) n++;
    return n;
}

/* strslice → NUL 结尾临时缓冲区（栈上，仅短名使用） */
static const char *slice_to_cstr_local(strslice_t s, char *buf, size_t cap) {
    if (s.len >= cap) s.len = cap - 1;
    memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';
    return buf;
}

/* 按类型宽度构造整数值（i8..u64） */
static value_t *make_int(ctfe_ctx_t *ctx, const type_t *t, uint64_t v) {
    void *data = value_alloc_data(ctx->vm->alloc, t);
    if (t == ctx->vm->type_i8)  *(int8_t  *)data = (int8_t)v;
    else if (t == ctx->vm->type_i16) *(int16_t *)data = (int16_t)v;
    else if (t == ctx->vm->type_i32) *(int32_t *)data = (int32_t)v;
    else if (t == ctx->vm->type_i64) *(int64_t *)data = (int64_t)v;
    else if (t == ctx->vm->type_u8)  *(uint8_t  *)data = (uint8_t)v;
    else if (t == ctx->vm->type_u16) *(uint16_t *)data = (uint16_t)v;
    else if (t == ctx->vm->type_u32) *(uint32_t *)data = (uint32_t)v;
    else if (t == ctx->vm->type_u64) *(uint64_t *)data = (uint64_t)v;
    else {
        /* 非整数类型：data 已分配，按错误路径释放 */
        allocator_free(ctx->vm->alloc, (void **)&data);
        return err(ctx, "ctfe: internal: make_int on non-integer type");
    }
    return value_make(ctx->vm, t, data);
}

/* AST_INT_LIT 类型后缀 → vm 整数类型；空后缀默认 i32 */
static const type_t *int_lit_type(ctfe_ctx_t *ctx, strslice_t suffix) {
    vm_t *vm = ctx->vm;
    if (strslice_is_empty(suffix)) return vm->type_i32;
    if (strslice_eq(suffix, STRSLICE_LIT("i8")))  return vm->type_i8;
    if (strslice_eq(suffix, STRSLICE_LIT("i16"))) return vm->type_i16;
    if (strslice_eq(suffix, STRSLICE_LIT("i32"))) return vm->type_i32;
    if (strslice_eq(suffix, STRSLICE_LIT("i64"))) return vm->type_i64;
    if (strslice_eq(suffix, STRSLICE_LIT("u8")))  return vm->type_u8;
    if (strslice_eq(suffix, STRSLICE_LIT("u16"))) return vm->type_u16;
    if (strslice_eq(suffix, STRSLICE_LIT("u32"))) return vm->type_u32;
    if (strslice_eq(suffix, STRSLICE_LIT("u64"))) return vm->type_u64;
    return NULL;
}

/* 读取 bool 值（调用方保证类型为 bool） */
static bool read_bool(vm_t *vm, value_t *v) {
    (void)vm;
    return *(bool *)value_data(v);
}

/* ===========================================================================
 * 前向声明
 * =========================================================================== */

static value_t *ctfe_eval_inner(ctfe_ctx_t *ctx, ast_node_t *node);

/* ===========================================================================
 * 函数调用
 * =========================================================================== */

/* 解释 AST_FUNC_DEF：绑定实参到临时局部作用域 → 解释函数体 → 返回 clone 到
   调用方 scope 的返回值（无显式 return → void）。 */
static value_t *ctfe_call_ast_fn(ctfe_ctx_t *ctx, ast_func_def_t *fn,
                                 ast_node_t *args) {
    vm_t *vm = ctx->vm;
    scope_t *caller_scope = vm->current_scope;

    /* 1. 临时局部作用域（参数 + 函数体变量） */
    vm_push_scope(vm);

    /* 2. 绑定参数：实参链与参数声明链逐对对应 */
    ast_node_t *a = args;
    for (ast_node_t *p = fn->params; p; p = p->next) {
        if (!a) {
            value_t *e = err(ctx, "ctfe: function call: too few arguments");
            vm_pop_scope(vm);
            return e;
        }
        value_t *av = ctfe_eval(ctx, a);
        if (value_is_error(vm, av)) {
            vm_pop_scope(vm);
            return av;
        }
        ast_var_def_t *vd = (ast_var_def_t *)p;
        char nb[128];
        const char *name = slice_to_cstr_local(vd->name, nb, sizeof nb);
        value_t *stored = scope_define(vm, vm->current_scope, name, av);
        if (value_is_error(vm, stored)) {
            vm_pop_scope(vm);
            return stored;
        }
        a = a->next;
    }
    if (a) {
        value_t *e = err(ctx, "ctfe: function call: too many arguments");
        vm_pop_scope(vm);
        return e;
    }

    /* 3. 解释函数体（body 为 AST_BLOCK，内部自带块作用域） */
    ctx->ctrl = CTFE_CTRL_NONE;
    ctx->ret_value = NULL;
    value_t *stmt_r = ctfe_eval_stmt(ctx, fn->body);
    if (value_is_error(vm, stmt_r)) {
        vm_pop_scope(vm);
        return stmt_r;
    }

    /* 4. 取返回值并 clone 到调用方 scope（必须在 pop 之前：ret_value 归
       callee scope，pop 会销毁） */
    value_t *ret = NULL;
    {
        vm->current_scope = caller_scope;
        if (ctx->ctrl == CTFE_CTRL_RETURN && ctx->ret_value) {
            ret = value_clone(vm, ctx->ret_value);
        } else {
            ret = value_make_undefined(vm);
        }
    }
    ctx->ctrl = CTFE_CTRL_NONE;
    ctx->ret_value = NULL;

    /* 5. pop 函数局部作用域 */
    vm_pop_scope(vm);
    vm->current_scope = caller_scope;

    return ret;
}

/* 调用已求值的 func value（内置 C 函数 / 运行时注册函数）：求值实参 → value_call */
static value_t *ctfe_call_value(ctfe_ctx_t *ctx, value_t *callee,
                                ast_node_t *args) {
    vm_t *vm = ctx->vm;
    size_t argc = count_siblings(args);
    value_t *av[argc > 0 ? argc : 1];
    size_t i = 0;
    for (ast_node_t *a = args; a; a = a->next) {
        av[i] = ctfe_eval(ctx, a);
        if (value_is_error(vm, av[i])) return av[i];
        i++;
    }
    return value_call(vm, callee, av, argc);
}

/* ===========================================================================
 * 表达式求值
 * =========================================================================== */

static value_t *ctfe_eval_inner(ctfe_ctx_t *ctx, ast_node_t *node) {
    vm_t *vm = ctx->vm;
    if (!node) return err(ctx, "ctfe: null expression node");

    switch (node->kind) {
    case AST_INT_LIT: {
        ast_int_lit_t *n = (ast_int_lit_t *)node;
        const type_t *t = int_lit_type(ctx, n->type);
        if (!t) return err(ctx, "ctfe: unsupported integer literal type");
        return make_int(ctx, t, n->value);
    }
    case AST_FLOAT_LIT: {
        ast_float_lit_t *n = (ast_float_lit_t *)node;
        if (strslice_eq(n->type, STRSLICE_LIT("f32"))) {
            float f = (float)n->value;
            void *data = value_alloc_data_copy(vm->alloc, vm->type_f32, &f);
            return value_make(vm, vm->type_f32, data);
        }
        void *data = value_alloc_data_copy(vm->alloc, vm->type_f64, &n->value);
        return value_make(vm, vm->type_f64, data);
    }
    case AST_BOOL_LIT: {
        ast_bool_lit_t *n = (ast_bool_lit_t *)node;
        void *data = value_alloc_data_copy(vm->alloc, vm->type_bool, &n->value);
        return value_make(vm, vm->type_bool, data);
    }
    case AST_CHAR_LIT: {
        ast_char_lit_t *n = (ast_char_lit_t *)node;
        return make_int(ctx, vm->type_u8, n->value);
    }
    case AST_STRING_LIT: {
        ast_string_lit_t *n = (ast_string_lit_t *)node;
        string_t *str = string_from_bytes(vm->alloc, n->text.ptr, n->text.len);
        void *data = value_alloc_data_copy(vm->alloc, vm->type_str, &str);
        return value_make(vm, vm->type_str, data);
    }
    case AST_IDENT: {
        ast_ident_t *n = (ast_ident_t *)node;
        value_t *v = scope_lookup(vm->current_scope, n->name);
        if (!v) return err(ctx, "ctfe: undefined variable (not compile-time)");
        return v; /* 借用引用，归 scope */
    }
    case AST_BINARY: {
        ast_binary_t *n = (ast_binary_t *)node;
        /* 短路 && / ||：惰性求值，不走 vtable 二元分派 */
        if (token_is(n->op, "&&") || token_is(n->op, "||")) {
            bool is_and = token_is(n->op, "&&");
            value_t *lhs = ctfe_eval(ctx, n->lhs);
            if (value_is_error(vm, lhs)) return lhs;
            if (value_type(lhs) != vm->type_bool)
                return err(ctx, "ctfe: '&&'/'||' requires bool operands");
            bool lv = read_bool(vm, lhs);
            if (is_and && !lv) {
                bool f = false;
                void *d = value_alloc_data_copy(vm->alloc, vm->type_bool, &f);
                return value_make(vm, vm->type_bool, d);
            }
            if (!is_and && lv) {
                bool t = true;
                void *d = value_alloc_data_copy(vm->alloc, vm->type_bool, &t);
                return value_make(vm, vm->type_bool, d);
            }
            value_t *rhs = ctfe_eval(ctx, n->rhs);
            if (value_is_error(vm, rhs)) return rhs;
            if (value_type(rhs) != vm->type_bool)
                return err(ctx, "ctfe: '&&'/'||' requires bool operands");
            return rhs;
        }
        value_t *lhs = ctfe_eval(ctx, n->lhs);
        if (value_is_error(vm, lhs)) return lhs;
        value_t *rhs = ctfe_eval(ctx, n->rhs);
        if (value_is_error(vm, rhs)) return rhs;
        if (token_is(n->op, "+"))  return value_add(vm, lhs, rhs);
        if (token_is(n->op, "-"))  return value_sub(vm, lhs, rhs);
        if (token_is(n->op, "*"))  return value_mul(vm, lhs, rhs);
        if (token_is(n->op, "/"))  return value_div(vm, lhs, rhs);
        if (token_is(n->op, "%"))  return value_mod(vm, lhs, rhs);
        if (token_is(n->op, "==")) return value_eq(vm, lhs, rhs);
        if (token_is(n->op, "!=")) return value_ne(vm, lhs, rhs);
        if (token_is(n->op, "<"))  return value_lt(vm, lhs, rhs);
        if (token_is(n->op, "<=")) return value_le(vm, lhs, rhs);
        if (token_is(n->op, ">"))  return value_gt(vm, lhs, rhs);
        if (token_is(n->op, ">=")) return value_ge(vm, lhs, rhs);
        if (token_is(n->op, "&"))  return value_band(vm, lhs, rhs);
        if (token_is(n->op, "|"))  return value_bor(vm, lhs, rhs);
        if (token_is(n->op, "^"))  return value_bxor(vm, lhs, rhs);
        if (token_is(n->op, "<<")) return value_shl(vm, lhs, rhs);
        if (token_is(n->op, ">>")) return value_shr(vm, lhs, rhs);
        return err(ctx, "ctfe: unsupported binary operator");
    }
    case AST_UNARY: {
        ast_unary_t *n = (ast_unary_t *)node;
        value_t *v = ctfe_eval(ctx, n->operand);
        if (value_is_error(vm, v)) return v;
        if (token_is(n->op, "-"))  return value_neg(vm, v);
        if (token_is(n->op, "!"))  return value_lnot(vm, v);
        if (token_is(n->op, "~"))  return value_bnot(vm, v);
        return err(ctx, "ctfe: unsupported unary operator");
    }
    case AST_CAST: {
        ast_cast_t *n = (ast_cast_t *)node;
        value_t *v = ctfe_eval(ctx, n->expr);
        if (value_is_error(vm, v)) return v;
        const type_t *target = type_find(vm, n->target_type);
        if (!target) return err(ctx, "ctfe: unknown cast target type");
        return value_explicit_cast(vm, v, target);
    }
    case AST_CALL: {
        ast_call_t *n = (ast_call_t *)node;
        if (n->callee && n->callee->kind == AST_IDENT) {
            ast_ident_t *id = (ast_ident_t *)n->callee;
            /* 1. vm scope 已有函数值（内置 printf / 注册函数）→ value_call */
            value_t *fnv = scope_lookup(vm->current_scope, id->name);
            if (fnv && value_type(fnv) && value_type(fnv)->vtable &&
                value_type(fnv)->vtable->call) {
                return ctfe_call_value(ctx, fnv, n->args);
            }
            /* 2. sema 符号表：AST_FUNC_DEF（clux 函数）→ 解释调用 */
            if (ctx->sema && ctx->sema->global_scope) {
                sema_symbol_t *sym =
                    sema_lookup(ctx->sema->global_scope, id->name);
                if (sym && sym->ast && sym->ast->kind == AST_FUNC_DEF) {
                    return ctfe_call_ast_fn(ctx, (ast_func_def_t *)sym->ast,
                                            n->args);
                }
            }
            return err(ctx, "ctfe: function not found (not compile-time)");
        }
        /* 3. 一般 callee：求值 → value_call */
        value_t *callee = ctfe_eval(ctx, n->callee);
        if (value_is_error(vm, callee)) return callee;
        return ctfe_call_value(ctx, callee, n->args);
    }
    case AST_UNDEF:
        return err(ctx, "ctfe: 'undefined' is not an expression");
    case AST_MEMBER:
        return err(ctx, "ctfe: member access is not supported in M1");
    case AST_INDEX:
        return err(ctx, "ctfe: index expression is not supported in M1");
    default:
        return err(ctx, "ctfe: unsupported expression node");
    }
}

value_t *ctfe_eval(ctfe_ctx_t *ctx, ast_node_t *node) {
    if (!ctx || !ctx->vm) return NULL;
    if (!node) return err(ctx, "ctfe: null expression node");
    if (ctx->budget == 0)
        return err(ctx, "ctfe: evaluation budget exceeded (possible loop)");
    ctx->budget--;
    if (ctx->depth >= ctx->max_depth)
        return err(ctx, "ctfe: evaluation depth exceeded");
    ctx->depth++;
    value_t *r = ctfe_eval_inner(ctx, node);
    ctx->depth--;
    return r;
}

/* ===========================================================================
 * 语句解释
 * =========================================================================== */

static value_t *ctfe_assign(ctfe_ctx_t *ctx, ast_assign_t *n) {
    vm_t *vm = ctx->vm;
    value_t *dst = scope_lookup(vm->current_scope, n->name);
    if (!dst) return err(ctx, "ctfe: assignment to undefined variable");
    value_t *src = ctfe_eval(ctx, n->value);
    if (value_is_error(vm, src)) return src;
    if (token_is(n->op, "=")) return value_assign(vm, dst, src);
    /* 复合赋值：+= -= *= /= %= */
    value_t *r = NULL;
    if (token_is(n->op, "+="))      r = value_add(vm, dst, src);
    else if (token_is(n->op, "-=")) r = value_sub(vm, dst, src);
    else if (token_is(n->op, "*=")) r = value_mul(vm, dst, src);
    else if (token_is(n->op, "/=")) r = value_div(vm, dst, src);
    else if (token_is(n->op, "%=")) r = value_mod(vm, dst, src);
    else return err(ctx, "ctfe: unsupported assignment operator");
    if (value_is_error(vm, r)) return r;
    return value_assign(vm, dst, r);
}

value_t *ctfe_eval_stmt(ctfe_ctx_t *ctx, ast_node_t *stmt) {
    vm_t *vm = ctx->vm;
    if (!ctx || !vm) return NULL;
    if (!stmt) return value_make_undefined(vm);
    if (ctx->budget == 0)
        return err(ctx, "ctfe: evaluation budget exceeded (possible loop)");
    ctx->budget--;

    switch (stmt->kind) {
    case AST_BLOCK: {
        ast_block_t *b = (ast_block_t *)stmt;
        vm_push_scope(vm);
        for (ast_node_t *s = b->stmts; s; s = s->next) {
            value_t *r = ctfe_eval_stmt(ctx, s);
            if (value_is_error(vm, r)) {
                vm_pop_scope(vm);
                return r;
            }
            if (ctx->ctrl != CTFE_CTRL_NONE) break;
        }
        vm_pop_scope(vm);
        return value_make_undefined(vm);
    }
    case AST_VAR_DEF: {
        ast_var_def_t *vd = (ast_var_def_t *)stmt;
        value_t *init = NULL;
        if (vd->init && vd->init->kind == AST_UNDEF) {
            /* 声明占位：分配声明类型零值 */
            const type_t *t = vd->type_name.len
                                  ? type_find(vm, vd->type_name) : NULL;
            if (!t) return err(ctx, "ctfe: undefined variable type");
            void *data = value_alloc_data(vm->alloc, t);
            init = value_make(vm, t, data);
        } else {
            init = ctfe_eval(ctx, vd->init);
            if (value_is_error(vm, init)) return init;
        }
        char nb[128];
        const char *name = slice_to_cstr_local(vd->name, nb, sizeof nb);
        value_t *stored = scope_define(vm, vm->current_scope, name, init);
        if (value_is_error(vm, stored)) return stored;
        return value_make_undefined(vm);
    }
    case AST_ASSIGN:
        return ctfe_assign(ctx, (ast_assign_t *)stmt);
    case AST_IF: {
        ast_if_t *n = (ast_if_t *)stmt;
        value_t *cv = ctfe_eval(ctx, n->cond);
        if (value_is_error(vm, cv)) return cv;
        if (value_type(cv) != vm->type_bool)
            return err(ctx, "ctfe: if condition must be bool");
        if (read_bool(vm, cv)) {
            value_t *r = ctfe_eval_stmt(ctx, n->then_body);
            if (value_is_error(vm, r)) return r;
        } else if (n->else_body) {
            value_t *r = ctfe_eval_stmt(ctx, n->else_body);
            if (value_is_error(vm, r)) return r;
        }
        return value_make_undefined(vm);
    }
    case AST_WHILE: {
        ast_while_t *n = (ast_while_t *)stmt;
        for (;;) {
            if (ctx->budget == 0)
                return err(ctx, "ctfe: evaluation budget exceeded (loop)");
            value_t *cv = ctfe_eval(ctx, n->cond);
            if (value_is_error(vm, cv)) return cv;
            if (value_type(cv) != vm->type_bool)
                return err(ctx, "ctfe: while condition must be bool");
            if (!read_bool(vm, cv)) break;
            value_t *r = ctfe_eval_stmt(ctx, n->body);
            if (value_is_error(vm, r)) return r;
            if (ctx->ctrl == CTFE_CTRL_BREAK) {
                ctx->ctrl = CTFE_CTRL_NONE;
                break;
            }
            if (ctx->ctrl == CTFE_CTRL_CONTINUE) ctx->ctrl = CTFE_CTRL_NONE;
            if (ctx->ctrl == CTFE_CTRL_RETURN) break;
        }
        return value_make_undefined(vm);
    }
    case AST_FOR: {
        ast_for_t *n = (ast_for_t *)stmt;
        vm_push_scope(vm); /* for 头变量（init 定义）与 body 同作用域 */
        if (n->init) {
            value_t *r = ctfe_eval_stmt(ctx, n->init);
            if (value_is_error(vm, r)) {
                vm_pop_scope(vm);
                return r;
            }
        }
        for (;;) {
            if (ctx->budget == 0) {
                vm_pop_scope(vm);
                return err(ctx, "ctfe: evaluation budget exceeded (loop)");
            }
            if (n->cond) {
                value_t *cv = ctfe_eval(ctx, n->cond);
                if (value_is_error(vm, cv)) {
                    vm_pop_scope(vm);
                    return cv;
                }
                if (value_type(cv) != vm->type_bool) {
                    vm_pop_scope(vm);
                    return err(ctx, "ctfe: for condition must be bool");
                }
                if (!read_bool(vm, cv)) break;
            }
            value_t *r = ctfe_eval_stmt(ctx, n->body);
            if (value_is_error(vm, r)) {
                vm_pop_scope(vm);
                return r;
            }
            if (ctx->ctrl == CTFE_CTRL_BREAK) {
                ctx->ctrl = CTFE_CTRL_NONE;
                break;
            }
            if (ctx->ctrl == CTFE_CTRL_CONTINUE) ctx->ctrl = CTFE_CTRL_NONE;
            if (ctx->ctrl == CTFE_CTRL_RETURN) break;
            if (n->update) {
                value_t *u = ctfe_eval_stmt(ctx, n->update);
                if (value_is_error(vm, u)) {
                    vm_pop_scope(vm);
                    return u;
                }
            }
        }
        vm_pop_scope(vm);
        return value_make_undefined(vm);
    }
    case AST_RETURN: {
        ast_return_t *n = (ast_return_t *)stmt;
        ctx->ret_value = n->value ? ctfe_eval(ctx, n->value)
                                  : value_make_undefined(vm);
        if (value_is_error(vm, ctx->ret_value)) return ctx->ret_value;
        ctx->ctrl = CTFE_CTRL_RETURN;
        return value_make_undefined(vm);
    }
    case AST_BREAK:
        ctx->ctrl = CTFE_CTRL_BREAK;
        return value_make_undefined(vm);
    case AST_CONTINUE:
        ctx->ctrl = CTFE_CTRL_CONTINUE;
        return value_make_undefined(vm);
    case AST_EXPR_STMT: {
        ast_expr_stmt_t *n = (ast_expr_stmt_t *)stmt;
        return ctfe_eval(ctx, n->expr);
    }
    default:
        /* 表达式直接作为语句（for 的 update 等）→ 求值并丢弃 */
        return ctfe_eval(ctx, stmt);
    }
}
