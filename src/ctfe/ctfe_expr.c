#include "ctfe/ctfe.h"

#include "core/panic.h"
#include "core/string.h"
#include "core/strslice.h"
#include "parser/ast_binary.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_call.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_const.h"
#include "parser/ast_volatile.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_ident.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_unary.h"
#include "parser/lexer.h"
#include "sema/symbol.h"
#include "vm/type.h"
#include "vm/type_error.h"
#include "vm/value.h"

/* ===========================================================================
 * 表达式求值
 *
 * 每个表达式产生真实 value（track 到 vm->current_scope）。失败返回 error
 * value。短路 &&/|| 惰性求值（不走 vtable 二元分派，与运行期解释器一致）。
 * =========================================================================== */

value_t *ctfe_eval_inner(ctfe_ctx_t *ctx, ast_node_t *node) {
    vm_t *vm = ctx->vm;
    if (!node) return ctfe_err(ctx, "ctfe: null expression node");

    switch (node->kind) {
    case AST_INT_LIT: {
        ast_int_lit_t *n = (ast_int_lit_t *)node;
        const type_t *t = ctfe_int_lit_type(ctx, n->type);
        if (!t) return ctfe_err(ctx, "ctfe: unsupported integer literal type");
        return ctfe_make_int(ctx, t, n->value);
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
        return ctfe_make_int(ctx, vm->type_u8, n->value);
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
        if (!v) return ctfe_err(ctx, "ctfe: undefined variable (not compile-time)");
        /* shadow value（data=NULL）是 sema 阶段运行期变量的占位：无真实
           数据可读，不是编译期常量 */
        if (value_is_shadow(v))
            return ctfe_err(ctx, "ctfe: variable is not a compile-time constant");
        return v; /* 借用引用，归 scope */
    }
    case AST_CONST: {
        ast_const_t *n = (ast_const_t *)node;
        value_t *sub = ctfe_eval(ctx, n->sub);
        if (value_is_error(vm, sub)) return sub;
        if (!value_is_type(sub, TYPE_KIND_TYPE))
            return ctfe_err(ctx, "ctfe: 'const' requires a type operand");
        const type_t *inner = value_as(sub, const type_t *);
        return type_as_value(vm, type_const_intern(vm, inner));
    }
    case AST_VOLATILE: {
        ast_volatile_t *n = (ast_volatile_t *)node;
        value_t *sub = ctfe_eval(ctx, n->sub);
        if (value_is_error(vm, sub)) return sub;
        if (!value_is_type(sub, TYPE_KIND_TYPE))
            return ctfe_err(ctx, "ctfe: 'volatile' requires a type operand");
        const type_t *inner = value_as(sub, const type_t *);
        return type_as_value(vm, type_volatile_intern(vm, inner));
    }
    case AST_BINARY: {
        ast_binary_t *n = (ast_binary_t *)node;
        /* 短路 && / ||：惰性求值，不走 vtable 二元分派 */
        if (token_is(n->op, "&&") || token_is(n->op, "||")) {
            bool is_and = token_is(n->op, "&&");
            value_t *lhs = ctfe_eval(ctx, n->lhs);
            if (value_is_error(vm, lhs)) return lhs;
            if (value_type(lhs) != vm->type_bool)
                return ctfe_err(ctx, "ctfe: '&&'/'||' requires bool operands");
            bool lv = ctfe_read_bool(vm, lhs);
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
                return ctfe_err(ctx, "ctfe: '&&'/'||' requires bool operands");
            return rhs;
        }
        /* as：显式类型转换。lhs/rhs 都按普通表达式求值——rhs 是类型
           表达式，求值结果应为 type value（data=type_t*）。遮蔽感知：
           类型名走 scope_lookup 与变量同机制。 */
        if (token_is(n->op, "as")) {
            value_t *lhs = ctfe_eval(ctx, n->lhs);
            if (value_is_error(vm, lhs)) return lhs;
            value_t *ty = ctfe_eval(ctx, n->rhs);
            if (value_is_error(vm, ty)) return ty;
            if (!value_is_type(ty, TYPE_KIND_TYPE))
                return ctfe_err(ctx, "ctfe: cast target must be a type");
            const type_t *target = value_as(ty, const type_t *);
            return value_explicit_cast(vm, lhs, target);
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
        return ctfe_err(ctx, "ctfe: unsupported binary operator");
    }
    case AST_UNARY: {
        ast_unary_t *n = (ast_unary_t *)node;
        value_t *v = ctfe_eval(ctx, n->operand);
        if (value_is_error(vm, v)) return v;
        if (token_is(n->op, "-"))  return value_neg(vm, v);
        if (token_is(n->op, "!"))  return value_lnot(vm, v);
        if (token_is(n->op, "~"))  return value_bnot(vm, v);
        return ctfe_err(ctx, "ctfe: unsupported unary operator");
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
            return ctfe_errf(ctx, "ctfe: undefined function '%.*s' (not compile-time)",
                        (int)id->name.len, id->name.ptr);
        }
        /* 3. 一般 callee：求值 → value_call */
        value_t *callee = ctfe_eval(ctx, n->callee);
        if (value_is_error(vm, callee)) return callee;
        return ctfe_call_value(ctx, callee, n->args);
    }
    case AST_UNDEF:
        return ctfe_err(ctx, "ctfe: 'undefined' is not an expression");
    case AST_MEMBER:
        return ctfe_err(ctx, "ctfe: member access is not supported in M1");
    case AST_INDEX:
        return ctfe_err(ctx, "ctfe: index expression is not supported in M1");
    default:
        return ctfe_err(ctx, "ctfe: unsupported expression node");
    }
}
