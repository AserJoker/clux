#include "sema/sema.h"
#include "core/string.h"
#include "parser/ast_binary.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_call.h"
#include "parser/ast_cast.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_error.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_ident.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_unary.h"
#include "parser/ast_undef.h"
#include "parser/lexer.h"
#include "vm/type_error.h"

#include <string.h>

/* ===========================================================================
 * 表达式 walker（shadow value 求值）
 *
 * 每个表达式节点返回一个 shadow value（只有类型，data=NULL）。shadow value
 * 经 vtable 运算路径，类型协商结果即为推导结果类型。错误恢复产物
 * （error/void shadow）沿运算传播但不级联二次诊断。
 *
 * 未初始化（TDZ）检查由确定性赋值分析在符号表 flow_init 上完成
 * （sema_lookup 跳过未激活符号），VM 值层不感知。
 * =========================================================================== */

/* 把操作数类型名写入诊断缓冲区 */
static void op_type_name(value_t *v, char *buf, size_t cap) {
  if (!v) {
    snprintf(buf, cap, "<none>");
    return;
  }
  const type_t *t = value_type(v);
  if (t && t->name.ptr)
    snprintf(buf, cap, "%.*s", (int)t->name.len, t->name.ptr);
  else
    snprintf(buf, cap, "<none>");
}

/* 检查操作数必须为 bool；错误/void shadow（错误恢复产物）静默通过 */
void sema_check_bool(sema_t *sema, ast_node_t *node, value_t *v,
                     const char *what) {
  if (value_is_error(sema->vm, v)) return;
  if (type_eq(value_type(v), sema->vm->type_void)) return;
  if (!type_eq(value_type(v), sema->vm->type_bool)) {
    char tn[64];
    op_type_name(v, tn, sizeof(tn));
    diag_error(sema->diag, sema_loc(sema, node), "%s operand must be bool, got %s",
               what, tn);
  }
}

/* 二元运算符 token → vtable 分派函数 */
static value_t *(*binop_of(const token_t *op))(vm_t *, value_t *, value_t *) {
  if (token_is(op, "+")) return value_add;
  if (token_is(op, "-")) return value_sub;
  if (token_is(op, "*")) return value_mul;
  if (token_is(op, "/")) return value_div;
  if (token_is(op, "%")) return value_mod;
  if (token_is(op, "==")) return value_eq;
  if (token_is(op, "!=")) return value_ne;
  if (token_is(op, "<")) return value_lt;
  if (token_is(op, "<=")) return value_le;
  if (token_is(op, ">")) return value_gt;
  if (token_is(op, ">=")) return value_ge;
  if (token_is(op, "&")) return value_band;
  if (token_is(op, "|")) return value_bor;
  if (token_is(op, "^")) return value_bxor;
  if (token_is(op, "<<")) return value_shl;
  if (token_is(op, ">>")) return value_shr;
  return NULL;
}

/* 从 token 取运算符文本（诊断用），写入 buf */
static void op_text(const token_t *op, char *buf, size_t cap) {
  size_t len = 0;
  const char *text = op ? token_get_text(op, &len) : NULL;
  if (!text || len == 0) {
    snprintf(buf, cap, "?");
    return;
  }
  size_t n = len < cap - 1 ? len : cap - 1;
  memcpy(buf, text, n);
  buf[n] = '\0';
}

static value_t *shadow_binary(sema_t *sema, ast_binary_t *node,
                              sema_scope_t *scope);

value_t *sema_expr(sema_t *sema, ast_node_t *node, sema_scope_t *scope) {
  if (!node) return value_make_shadow(sema->vm, sema->vm->type_void);
  switch (node->kind) {
    case AST_INT_LIT: {
      ast_int_lit_t *n = (ast_int_lit_t *)node;
      const type_t *t = n->type.len ? resolve_type(sema, n->type)
                                    : sema->vm->type_i32;
      if (!t) {
        diag_error(sema->diag, sema_loc(sema, node), "unknown type '%.*s'",
                   (int)n->type.len, n->type.ptr);
        t = sema->vm->type_i32;
      }
      return value_make_shadow(sema->vm, t);
    }
    case AST_FLOAT_LIT: {
      ast_float_lit_t *n = (ast_float_lit_t *)node;
      const type_t *t = n->type.len ? resolve_type(sema, n->type)
                                    : sema->vm->type_f64;
      if (!t) {
        diag_error(sema->diag, sema_loc(sema, node), "unknown type '%.*s'",
                   (int)n->type.len, n->type.ptr);
        t = sema->vm->type_f64;
      }
      return value_make_shadow(sema->vm, t);
    }
    case AST_BOOL_LIT:
      return value_make_shadow(sema->vm, sema->vm->type_bool);
    case AST_CHAR_LIT:
      return value_make_shadow(sema->vm, sema->vm->type_u8);
    case AST_STRING_LIT:
      return value_make_shadow(sema->vm, sema->vm->type_str);
    case AST_IDENT: {
      /* 变量读取：从 VM scope 链 lookup shadow value（与 sema 作用域树同构，
         天然遮罩）。未初始化检查走符号表 flow_init（确定性赋值分析，
         VM 值层不感知 TDZ）。 */
      ast_ident_t *n = (ast_ident_t *)node;
      value_t *v = scope_lookup(sema->vm->current_scope, n->name);
      if (!v) {
        diag_error(sema->diag, sema_loc(sema, node),
                   "undefined variable '%.*s'", (int)n->name.len, n->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      sema_symbol_t *sym = sema_lookup(scope, n->name);
      if (sym && !sym->flow_init) {
        diag_error(sema->diag, sema_loc(sema, node),
                   "variable '%.*s' used before initialization",
                   (int)n->name.len, n->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      return value_make_shadow(sema->vm, value_type(v));
    }
    case AST_UNDEF:
      /* undefined 只允许作为 var 初始化的"未初始化声明"（shadow_var_def
         消费）；普通表达式位置引用是非法用法。 */
      diag_error(sema->diag, sema_loc(sema, node),
                 "'undefined' can only be used as a variable initializer");
      return value_make_shadow(sema->vm, sema->vm->type_void);
    case AST_BINARY:
      return shadow_binary(sema, (ast_binary_t *)node, scope);
    case AST_UNARY: {
      ast_unary_t *n = (ast_unary_t *)node;
      value_t *operand = sema_expr(sema, n->operand, scope);
      /* 错误恢复产物静默通过，避免级联二次诊断 */
      if (value_is_error(sema->vm, operand) ||
          type_eq(value_type(operand), sema->vm->type_void))
        return value_make_shadow(sema->vm, sema->vm->type_void);
      value_t *result = NULL;
      if (token_is(n->op, "-")) {
        result = value_neg(sema->vm, operand);
      } else if (token_is(n->op, "!")) {
        sema_check_bool(sema, n->operand, operand, "logical not");
        result = value_lnot(sema->vm, operand);
      } else if (token_is(n->op, "~")) {
        result = value_bnot(sema->vm, operand);
      } else {
        char ob[16];
        op_text(n->op, ob, sizeof(ob));
        diag_error(sema->diag, sema_loc(sema, node),
                   "unsupported unary operator '%s'", ob);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      if (value_is_error(sema->vm, result)) {
        char ob[16], tn[64];
        op_text(n->op, ob, sizeof(ob));
        op_type_name(operand, tn, sizeof(tn));
        diag_error(sema->diag, sema_loc(sema, node),
                   "operator '%s' cannot be applied to %s", ob, tn);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      return result;
    }
    case AST_CALL: {
      /* 函数调用：符号查找 + 实参 shadow 求值（sema 职责）→ 构造带签名
         type_t 的 shadow callee → value_call 分派到 func_vcall 的 shadow
         分支（唯一校验点：参数数量/隐式转换，不执行 cfunc）→ 错误翻译为诊断 */
      ast_call_t *call = (ast_call_t *)node;
      if (!call->callee || call->callee->kind != AST_IDENT) {
        diag_error(sema->diag, sema_loc(sema, &call->base),
                   "M1: callee must be a function name");
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      ast_ident_t *name = (ast_ident_t *)call->callee;
      sema_symbol_t *sym = sema_lookup(sema->global_scope, name->name);
      /* 函数符号：用户函数 sym->ast=AST_FUNC_DEF；内置函数（printf）ast=NULL
         但 type 携带 variadic 签名。两者都经 func_shadow_call 校验。 */
      if (!sym || !sym->type) {
        diag_error(sema->diag, sema_loc(sema, &call->base),
                   "undefined function '%.*s'", (int)name->name.len,
                   name->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }

      /* 逐个实参 shadow 求值（错误恢复产物保留为 void shadow，由
         func_shadow_call 跳过，避免级联二次诊断） */
      size_t argc = sema_count_siblings(call->args);
      value_t *arg_shadows[argc > 0 ? argc : 1];
      ast_node_t *arg = call->args;
      for (size_t i = 0; arg; arg = arg->next, i++) {
        arg_shadows[i] = sema_expr(sema, arg, scope);
      }

      /* shadow callee：data=NULL 只带签名类型（sym->type），受 vm scope
         管理（auto-track） */
      value_t *callee_shadow = value_make_shadow(sema->vm, sym->type);
      value_t *result = value_call(sema->vm, callee_shadow, arg_shadows, argc);

      if (value_is_error(sema->vm, result)) {
        error_data_t *ed = (error_data_t *)value_data(result);
        const char *msg = ed && ed->message ? string_cstr(ed->message)
                                            : "function call failed";
        diag_error(sema->diag, sema_loc(sema, &call->base), "%s", msg);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      return result; /* shadow in → shadow out（return_type shadow） */
    }
    case AST_CAST: {
      ast_cast_t *n = (ast_cast_t *)node;
      value_t *expr = sema_expr(sema, n->expr, scope);
      const type_t *target = resolve_type(sema, n->target_type);
      if (!target) {
        diag_error(sema->diag, sema_loc(sema, node), "unknown type '%.*s'",
                   (int)n->target_type.len, n->target_type.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      value_t *result = value_explicit_cast(sema->vm, expr, target);
      if (value_is_error(sema->vm, result)) {
        char tn[64], tt[64];
        op_type_name(expr, tn, sizeof(tn));
        sema_type_name(target, tt, sizeof(tt));
        diag_error(sema->diag, sema_loc(sema, node), "cannot cast %s to %s",
                   tn, tt);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      return result;
    }
    case AST_MEMBER:
      diag_error(sema->diag, sema_loc(sema, node),
                 "member access is not supported in M1");
      return value_make_shadow(sema->vm, sema->vm->type_void);
    case AST_INDEX:
      diag_error(sema->diag, sema_loc(sema, node),
                 "indexing is not supported in M1");
      return value_make_shadow(sema->vm, sema->vm->type_void);
    case AST_ERROR:
    default:
      return value_make_shadow(sema->vm, sema->vm->type_void);
  }
}

static value_t *shadow_binary(sema_t *sema, ast_binary_t *node,
                              sema_scope_t *scope) {
  /* 短路 && / ||：操作数必须 bool，结果 bool */
  if (token_is(node->op, "&&") || token_is(node->op, "||")) {
    value_t *lhs = sema_expr(sema, node->lhs, scope);
    sema_check_bool(sema, node->lhs, lhs, "logical operator");
    value_t *rhs = sema_expr(sema, node->rhs, scope);
    sema_check_bool(sema, node->rhs, rhs, "logical operator");
    return value_make_shadow(sema->vm, sema->vm->type_bool);
  }

  value_t *lhs = sema_expr(sema, node->lhs, scope);
  value_t *rhs = sema_expr(sema, node->rhs, scope);
  /* 错误恢复产物（error/void shadow）静默通过，避免级联二次诊断 */
  if (value_is_error(sema->vm, lhs) || value_is_error(sema->vm, rhs) ||
      type_eq(value_type(lhs), sema->vm->type_void) ||
      type_eq(value_type(rhs), sema->vm->type_void))
    return value_make_shadow(sema->vm, sema->vm->type_void);
  value_t *(*op)(vm_t *, value_t *, value_t *) = binop_of(node->op);
  if (!op) {
    char ob[16];
    op_text(node->op, ob, sizeof(ob));
    diag_error(sema->diag, sema_loc(sema, &node->base),
               "unsupported binary operator '%s'", ob);
    return value_make_shadow(sema->vm, sema->vm->type_void);
  }
  value_t *result = op(sema->vm, lhs, rhs);
  if (value_is_error(sema->vm, result)) {
    char ob[16], ln[64], rn[64];
    op_text(node->op, ob, sizeof(ob));
    op_type_name(lhs, ln, sizeof(ln));
    op_type_name(rhs, rn, sizeof(rn));
    diag_error(sema->diag, sema_loc(sema, &node->base),
               "type mismatch: cannot apply '%s' to %s and %s", ob, ln, rn);
    return value_make_shadow(sema->vm, sema->vm->type_void);
  }
  return result; /* shadow in → shadow out */
}
