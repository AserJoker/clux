#include "sema/sema.h"
#include "core/panic.h"
#include "parser/ast_binary.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_call.h"
#include "parser/ast_cast.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_error.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_func_def.h"
#include "parser/ast_ident.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_program.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_unary.h"
#include "parser/ast_var_def.h"
#include "parser/lexer.h"
#include <string.h>

/* ===========================================================================
 * 上下文管理
 * =========================================================================== */

sema_t *sema_create(vm_t *vm, diag_buf_t *diag, vec_t *tokens) {
  if (!vm || !diag || !tokens) return NULL;
  sema_t *sema =
      allocator_new_ex(vm->alloc, "sema_t", sizeof(sema_t), NULL, NULL, NULL,
                       1);
  sema->vm = vm;
  sema->diag = diag;
  sema->tokens = tokens;
  sema->global_scope = NULL;
  sema->func_return_type = NULL;
  sema->func_has_return = false;
  sema->loop_depth = 0;
  return sema;
}

void sema_destroy(sema_t **sema) {
  if (!sema || !*sema) return;
  allocator_free((*sema)->vm->alloc, (void **)sema);
}

/* ===========================================================================
 * 公共工具
 * =========================================================================== */

const type_t *resolve_type(sema_t *sema, strslice_t name) {
  return type_find(sema->vm, name);
}

location_t sema_loc(sema_t *sema, ast_node_t *node) {
  location_t zero = {0};
  if (!sema || !node) return zero;
  const token_t *t = (const token_t *)vec_get(sema->tokens, node->tok_begin);
  const location_t *loc = t ? token_get_location(t) : NULL;
  return loc ? *loc : zero;
}

void sema_type_name(const type_t *t, char *buf, size_t cap) {
  if (!buf || cap == 0) return;
  buf[0] = '\0';
  if (!t || !t->name.ptr) return;
  size_t n = t->name.len < cap - 1 ? t->name.len : cap - 1;
  memcpy(buf, t->name.ptr, n);
  buf[n] = '\0';
}

size_t sema_count_siblings(const ast_node_t *node) {
  size_t n = 0;
  for (const ast_node_t *p = node; p; p = p->next) n++;
  return n;
}

bool sema_type_assignable(sema_t *sema, const type_t *dst, const type_t *src) {
  if (!dst || !src) return false; /* 待推断类型不可比较 */
  if (type_eq(dst, src)) return true;
  value_t *s = value_make_shadow(sema->vm, src);
  value_t *c = value_implicit_cast(sema->vm, s, dst);
  return !value_is_error(sema->vm, c);
}

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

/* ===========================================================================
 * 表达式 walker（shadow value 求值）
 * =========================================================================== */

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

static value_t *shadow_call(sema_t *sema, ast_call_t *node,
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
      ast_ident_t *n = (ast_ident_t *)node;
      sema_symbol_t *sym = sema_lookup(scope, n->name);
      if (!sym) {
        diag_error(sema->diag, sema_loc(sema, node),
                   "undefined variable '%.*s'", (int)n->name.len, n->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      if (sym->is_tdz) {
        diag_error(sema->diag, sema_loc(sema, node),
                   "variable '%.*s' used before initialization",
                   (int)n->name.len, n->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      return value_make_shadow(sema->vm,
                               sym->type ? sym->type : sema->vm->type_void);
    }
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
    case AST_CALL:
      return shadow_call(sema, (ast_call_t *)node, scope);
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

/* shadow 版本的函数调用：校验签名后直接构造 return_type 的 shadow value。
   value_call / func_vcall 完全不动（运行时语义保持）。 */
static value_t *shadow_call(sema_t *sema, ast_call_t *node,
                            sema_scope_t *scope) {
  if (!node->callee || node->callee->kind != AST_IDENT) {
    diag_error(sema->diag, sema_loc(sema, &node->base),
               "M1: callee must be a function name");
    return value_make_shadow(sema->vm, sema->vm->type_void);
  }
  ast_ident_t *name = (ast_ident_t *)node->callee;
  sema_symbol_t *sym = sema_lookup(sema->global_scope, name->name);
  if (!sym || !sym->func) {
    diag_error(sema->diag, sema_loc(sema, &node->base),
               "undefined function '%.*s'", (int)name->name.len,
               name->name.ptr);
    return value_make_shadow(sema->vm, sema->vm->type_void);
  }
  func_t *fn = sym->func;

  /* 参数数量校验（与 func_vcall 一致：variadic 允许 argc > param_count） */
  size_t argc = sema_count_siblings(node->args);
  if (!fn->is_variadic && argc != fn->param_count) {
    diag_error(sema->diag, sema_loc(sema, &node->base),
               "function '%.*s' expects %zu arguments, got %zu",
               (int)fn->name.len, fn->name.ptr, fn->param_count, argc);
  } else if (fn->is_variadic && argc < fn->param_count) {
    diag_error(sema->diag, sema_loc(sema, &node->base),
               "variadic function '%.*s' expects at least %zu arguments",
               (int)fn->name.len, fn->name.ptr, fn->param_count);
  }

  /* 逐个参数：shadow 求值 + implicit_cast 校验（与 func_vcall 对齐） */
  ast_node_t *arg = node->args;
  for (size_t i = 0; arg; i++, arg = arg->next) {
    value_t *av = sema_expr(sema, arg, scope);
    /* 错误恢复产物（void/error shadow）跳过，避免二次诊断 */
    if (value_is_error(sema->vm, av)) continue;
    if (type_eq(value_type(av), sema->vm->type_void)) continue;
    if (i < fn->param_count && fn->params[i] &&
        !type_eq(value_type(av), fn->params[i])) {
      value_t *casted = value_implicit_cast(sema->vm, av, fn->params[i]);
      if (value_is_error(sema->vm, casted)) {
        char an[64], pn[64];
        op_type_name(av, an, sizeof(an));
        sema_type_name(fn->params[i], pn, sizeof(pn));
        diag_error(sema->diag, sema_loc(sema, arg),
                   "argument %zu: cannot convert %s to %s", i + 1, an, pn);
      }
    }
    /* variadic 额外参数：求值但类型不限（printf 的 ...） */
  }

  /* shadow 版本：不执行 cfunc、不做作用域切换，直接构造 return_type shadow */
  return value_make_shadow(sema->vm,
                           fn->return_type ? fn->return_type
                                           : sema->vm->type_void);
}

/* ===========================================================================
 * Pass 1/2：函数名收集 + 类型解析（func_t 签名）
 * =========================================================================== */

static void pass1_names(sema_t *sema, ast_program_t *prog) {
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    ast_func_def_t *fn = (ast_func_def_t *)f;
    sema_symbol_t init = {.is_active = true}; /* 函数定义顺序自由，立即激活 */
    if (!sema_scope_define(sema->global_scope, fn->name, &init)) {
      diag_error(sema->diag, sema_loc(sema, f), "duplicate function '%.*s'",
                 (int)fn->name.len, fn->name.ptr);
    }
  }
}

static void pass2_types(sema_t *sema, ast_program_t *prog) {
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    ast_func_def_t *fn = (ast_func_def_t *)f;
    sema_symbol_t *sym = sema_lookup(sema->global_scope, fn->name);
    if (!sym) continue; /* Pass 1 重复定义已诊断 */
    if (sym->func) continue; /* 重复函数名：首个定义已建签名（Pass 1 已诊断） */

    size_t n = sema_count_siblings(fn->params);
    const type_t **params = NULL;
    if (n > 0) {
      params = allocator_new_ex(sema->vm->alloc, "type_t*", sizeof(type_t *),
                                NULL, NULL, NULL, n);
      size_t i = 0;
      for (ast_node_t *p = fn->params; p; p = p->next, i++) {
        ast_var_def_t *vd = (ast_var_def_t *)p;
        const type_t *t = resolve_type(sema, vd->type_name);
        if (!t) {
          diag_error(sema->diag, sema_loc(sema, p),
                     "unknown type '%.*s' in parameter '%.*s'",
                     (int)vd->type_name.len, vd->type_name.ptr,
                     (int)vd->name.len, vd->name.ptr);
        }
        params[i] = t; /* 失败置 NULL，位置对齐，shadow_call 校验时跳过 */
      }
    }

    const type_t *rt = NULL;
    if (fn->return_type.len) {
      rt = resolve_type(sema, fn->return_type);
      if (!rt) {
        diag_error(sema->diag, sema_loc(sema, f), "unknown return type '%.*s'",
                   (int)fn->return_type.len, fn->return_type.ptr);
      }
    }

    func_t *func = func_new(sema->vm->alloc, NULL, NULL, NULL, fn->name);
    func->params = params;
    func->param_count = n;
    func->return_type = rt;
    func->is_variadic = false;
    sym->func = func;
  }
}

/* ===========================================================================
 * 三遍编排
 * =========================================================================== */

bool sema_analyze(sema_t *sema, ast_node_t *program) {
  if (!sema || !program || program->kind != AST_PROGRAM) return false;
  ast_program_t *prog = (ast_program_t *)program;

  sema->global_scope =
      sema_scope_new(sema->vm->alloc, SEMA_SCOPE_GLOBAL, NULL);
  if (!sema->global_scope) return false;

  pass1_names(sema, prog);
  pass2_types(sema, prog);

  /* Pass 3a：作用域树构建 + 控制流分析（符号注册、break/continue 位置检查、
     不可达语句、非 void 函数返回路径完整性）。快速失败：3a 有错误则
     不进入 3b——控制流/结构错误已使作用域树不可信，继续 shadow run
     只会产生级联的二次诊断。 */
  sema_build_scope_tree(sema, program);
  if (diag_has_error(sema->diag)) return false;

  /* Pass 3b：shadow VM 运行（按作用域树严格对应遍历，纯类型检查与推导） */
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    sema_walk_function(sema, f);
  }

  return !diag_has_error(sema->diag);
}
