#include "sema/sema.h"
#include "core/panic.h"
#include "core/string.h"
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
#include "vm/type_error.h"
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
  sema->funcs = vec_new(vm->alloc, false); /* 元素手动释放（sema_func_t 无 dispose） */
  sema->func_return_type = NULL;
  sema->func_has_return = false;
  sema->loop_depth = 0;
  return sema;
}

void sema_destroy(sema_t **sema) {
  if (!sema || !*sema) return;
  allocator_t *alloc = (*sema)->vm->alloc;
  if ((*sema)->funcs) {
    size_t n = vec_len((*sema)->funcs);
    for (size_t i = 0; i < n; i++) {
      sema_func_t *sf = (sema_func_t *)vec_get((*sema)->funcs, i);
      allocator_free(alloc, (void **)&sf);
    }
    vec_free(alloc, &(*sema)->funcs);
  }
  allocator_free(alloc, (void **)sema);
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
         天然遮罩）。TDZ 状态在 value 上：未初始化读取报错。 */
      ast_ident_t *n = (ast_ident_t *)node;
      value_t *v = scope_lookup(sema->vm->current_scope, n->name);
      if (!v) {
        diag_error(sema->diag, sema_loc(sema, node),
                   "undefined variable '%.*s'", (int)n->name.len, n->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      if (value_is_tdz(v)) {
        diag_error(sema->diag, sema_loc(sema, node),
                   "variable '%.*s' used before initialization",
                   (int)n->name.len, n->name.ptr);
        return value_make_shadow(sema->vm, sema->vm->type_void);
      }
      return value_make_shadow(sema->vm, value_type(v));
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
      if (!sym || !sym->ast) {
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

/* ===========================================================================
 * Pass 1/2：函数名收集 + 类型解析（func_t 签名）
 * =========================================================================== */

static void pass1_names(sema_t *sema, ast_program_t *prog) {
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    ast_func_def_t *fn = (ast_func_def_t *)f;
    sema_symbol_t init = {0}; /* 函数定义顺序自由：Pass 1 全部注册，无遮罩问题 */
    if (!sema_scope_define(sema->global_scope, fn->name, &init)) {
      diag_error(sema->diag, sema_loc(sema, f), "duplicate function '%.*s'",
                 (int)fn->name.len, fn->name.ptr);
      continue; /* 重复定义不入队 */
    }
    /* 登记 sema 层函数对象（Pass 3 队列驱动；局部函数/泛型实例将来追加） */
    sema_func_t *sf = allocator_new_ex(sema->vm->alloc, "sema_func_t",
                                       sizeof(sema_func_t), NULL, NULL, NULL,
                                       1);
    if (!sf) panic("sema: out of memory allocating sema_func");
    sf->def = f;
    sf->scope = NULL;
    sf->name = fn->name;
    vec_push(sema->funcs, sema->vm->alloc, sf);
  }
}

static void pass2_types(sema_t *sema) {
  size_t n = vec_len(sema->funcs);
  for (size_t i = 0; i < n; i++) {
    sema_func_t *sf = (sema_func_t *)vec_get(sema->funcs, i);
    ast_node_t *f = sf->def;
    ast_func_def_t *fn = (ast_func_def_t *)f;
    sema_symbol_t *sym = sema_lookup(sema->global_scope, fn->name);
    if (!sym) continue;
    if (sym->ast) continue; /* 理论上不可达：队列只含有效函数 */

    size_t nparams = sema_count_siblings(fn->params);
    const type_t **params = NULL;
    if (nparams > 0) {
      params = allocator_new_ex(sema->vm->alloc, "type_t*", sizeof(type_t *),
                                NULL, NULL, NULL, nparams);
      size_t j = 0;
      for (ast_node_t *p = fn->params; p; p = p->next, j++) {
        ast_var_def_t *vd = (ast_var_def_t *)p;
        const type_t *t = resolve_type(sema, vd->type_name);
        if (!t) {
          diag_error(sema->diag, sema_loc(sema, p),
                     "unknown type '%.*s' in parameter '%.*s'",
                     (int)vd->type_name.len, vd->type_name.ptr,
                     (int)vd->name.len, vd->name.ptr);
        }
        params[j] = t; /* 失败置 NULL，位置对齐，func_shadow_call 校验时跳过 */
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

    /* 注册签名类型（按签名去重 intern 到 vm 类型池，type_func_sig 复制 params）；
       符号统一记录定义 AST 节点（函数 = AST_FUNC_DEF）；签名类型存于
       sym->type，调用点经 value_make_shadow(vm, sym->type) 构造 shadow callee */
    const type_t *sig = type_func_sig(sema->vm, params, nparams, rt, false);
    sym->type = sig;
    sym->ast = f;

    /* params 临时数组已被 type_func_sig 复制，此处释放 */
    if (params) allocator_free(sema->vm->alloc, (void **)&params);
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
  pass2_types(sema);

  /* Pass 3a：作用域树构建 + 控制流分析（符号注册、break/continue 位置检查、
     不可达语句、非 void 函数返回路径完整性）。快速失败：3a 有错误则
     不进入 3b——控制流/结构错误已使作用域树不可信，继续 shadow run
     只会产生级联的二次诊断。 */
  sema_build_scope_tree(sema);
  if (diag_has_error(sema->diag)) return false;

  /* Pass 3b：shadow VM 运行（按作用域树严格对应遍历，纯类型检查与推导）。
     队列驱动：遍历 sema->funcs，解析过程中队列可增长（局部函数提升 /
     泛型单态化追加到队尾），len 每次重取自动覆盖新函数。 */
  for (size_t i = 0; i < vec_len(sema->funcs); i++) {
    sema_walk_function(sema, (sema_func_t *)vec_get(sema->funcs, i));
  }

  return !diag_has_error(sema->diag);
}
