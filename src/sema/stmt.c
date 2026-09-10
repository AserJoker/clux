#include "sema/sema.h"
#include "parser/ast_assign.h"
#include "parser/ast_block.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_for.h"
#include "parser/ast_func_def.h"
#include "parser/ast_if.h"
#include "parser/ast_program.h"
#include "parser/ast_return.h"
#include "parser/ast_var_def.h"
#include "parser/ast_while.h"
#include "parser/lexer.h"
#include <stdio.h>
#include <string.h>

/* ===========================================================================
 * Pass 3a：作用域树构建 + 控制流分析
 *
 * 遍历函数体，按词法块结构建树。只注册符号（名字 + 声明类型 + TDZ 标志），
 * 不做类型检查。推断类型的变量 type=NULL，留给 Pass 3b 填充。
 * 子作用域按出现顺序追加（Pass 3b 每层独立索引按序取用，严格对应）。
 *
 * 建树同时做纯结构性的返回路径完整性分析（block_result_t.definitely_returns）：
 * 与类型无关，因此不需要等 Pass 3b 的 shadow 运行。非 void 函数所有路径
 * 必须 return 在此阶段即可检查。
 * =========================================================================== */

typedef struct build_result {
  bool definitely_returns; /* 该块保证返回（所有路径都 return） */
} build_result_t;

static build_result_t build_block(sema_t *sema, ast_block_t *block,
                                  sema_scope_t *scope);
static build_result_t build_func_if(sema_t *sema, ast_if_t *it,
                                    sema_scope_t *scope);

static void build_func(sema_t *sema, sema_func_t *sf) {
  ast_func_def_t *fn = (ast_func_def_t *)sf->def;

  sema_scope_t *fscope =
      sema_scope_new(sema->vm->alloc, SEMA_SCOPE_FUNCTION, sema->global_scope);
  sema_scope_add_child(sema->global_scope, fscope);
  sf->scope = fscope;

  /* 注册参数（已解析类型，运行时值在 Pass 3b 进入函数时定义到 VM scope） */
  for (ast_node_t *p = fn->params; p; p = p->next) {
    ast_var_def_t *vd = (ast_var_def_t *)p;
    sema_symbol_t init = {.type = resolve_type(sema, vd->type_name)};
    if (!sema_scope_define(fscope, vd->name, &init)) {
      diag_error(sema->diag, sema_loc(sema, p),
                 "duplicate parameter '%.*s'", (int)vd->name.len,
                 vd->name.ptr);
    }
  }

  /* 函数体 block 直接用 fscope（不再嵌套一层） */
  build_result_t r = build_block(sema, (ast_block_t *)fn->body, fscope);

  /* 控制流分析：非 void 函数所有路径必须 return（纯结构，不依赖类型） */
  const type_t *rt = fn->return_type.len ? resolve_type(sema, fn->return_type)
                                         : NULL;
  if (rt && !type_eq(rt, sema->vm->type_void) && !r.definitely_returns) {
    diag_error(sema->diag, sema_loc(sema, &fn->base),
               "function '%.*s' must return a value on all paths",
               (int)fn->name.len, fn->name.ptr);
  }
}

static build_result_t build_block(sema_t *sema, ast_block_t *block,
                                  sema_scope_t *scope) {
  build_result_t r = {0};
  for (ast_node_t *s = block->stmts; s; s = s->next) {
    if (r.definitely_returns) {
      /* 严格检查：return 后不可达语句报错。
         不建作用域、不注册符号——Pass 3b 同步跳过（索引保持对齐）。 */
      diag_error(sema->diag, sema_loc(sema, s), "unreachable statement");
      continue;
    }
    switch (s->kind) {
      case AST_VAR_DEF: {
        ast_var_def_t *vd = (ast_var_def_t *)s;
        const type_t *vt = NULL;
        if (vd->type_name.len) {
          vt = resolve_type(sema, vd->type_name);
          if (!vt) {
            diag_error(sema->diag, sema_loc(sema, s), "unknown type '%.*s'",
                       (int)vd->type_name.len, vd->type_name.ptr);
          }
        }
        sema_symbol_t init = {.type = vt};
        if (!sema_scope_define(scope, vd->name, &init)) {
          diag_error(sema->diag, sema_loc(sema, s),
                     "duplicate variable '%.*s'", (int)vd->name.len,
                     vd->name.ptr);
        }
        break;
      }
      case AST_BLOCK: {
        sema_scope_t *child =
            sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, scope);
        sema_scope_add_child(scope, child);
        build_result_t cr = build_block(sema, (ast_block_t *)s, child);
        if (cr.definitely_returns) r.definitely_returns = true;
        break;
      }
      case AST_IF: {
        ast_if_t *it = (ast_if_t *)s;
        sema_scope_t *then_scope =
            sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, scope);
        sema_scope_add_child(scope, then_scope);
        build_result_t tr = build_block(sema, (ast_block_t *)it->then_body,
                                        then_scope);
        build_result_t er = {0};
        if (it->else_body) {
          if (it->else_body->kind == AST_IF) {
            /* else-if 链：同层递归（子作用域顺序与 3b 一致） */
            er = build_func_if(sema, (ast_if_t *)it->else_body, scope);
          } else {
            sema_scope_t *else_scope =
                sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, scope);
            sema_scope_add_child(scope, else_scope);
            er = build_block(sema, (ast_block_t *)it->else_body, else_scope);
          }
        }
        if (tr.definitely_returns && er.definitely_returns)
          r.definitely_returns = true;
        break;
      }
      case AST_WHILE: {
        ast_while_t *wl = (ast_while_t *)s;
        sema_scope_t *body_scope =
            sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, scope);
        sema_scope_add_child(scope, body_scope);
        sema->loop_depth++;
        build_block(sema, (ast_block_t *)wl->body, body_scope);
        sema->loop_depth--;
        break; /* 循环体可能不执行，不贡献 definitely_returns */
      }
      case AST_FOR: {
        ast_for_t *fr = (ast_for_t *)s;
        sema_scope_t *for_scope =
            sema_scope_new(sema->vm->alloc, SEMA_SCOPE_FOR, scope);
        sema_scope_add_child(scope, for_scope);
        /* init 变量注册到 for scope */
        if (fr->init && fr->init->kind == AST_VAR_DEF) {
          ast_var_def_t *vd = (ast_var_def_t *)fr->init;
          const type_t *vt = NULL;
          if (vd->type_name.len) {
            vt = resolve_type(sema, vd->type_name);
            if (!vt) {
              diag_error(sema->diag, sema_loc(sema, fr->init),
                         "unknown type '%.*s'", (int)vd->type_name.len,
                         vd->type_name.ptr);
            }
          }
          sema_symbol_t init_sym = {.type = vt};
          if (!sema_scope_define(for_scope, vd->name, &init_sym)) {
            diag_error(sema->diag, sema_loc(sema, fr->init),
                       "duplicate variable '%.*s'", (int)vd->name.len,
                       vd->name.ptr);
          }
        }
        /* body 是 for scope 的子 scope */
        sema_scope_t *body_scope =
            sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, for_scope);
        sema_scope_add_child(for_scope, body_scope);
        sema->loop_depth++;
        build_block(sema, (ast_block_t *)fr->body, body_scope);
        sema->loop_depth--;
        break; /* 循环体可能不执行，不贡献 definitely_returns */
      }
      case AST_RETURN:
        r.definitely_returns = true;
        break; /* 后续语句在循环顶部报 unreachable */
      case AST_BREAK:
      case AST_CONTINUE:
        if (sema->loop_depth == 0) {
          diag_error(sema->diag, sema_loc(sema, s), "'%.*s' outside loop",
                     (int)(s->kind == AST_BREAK ? 5 : 8),
                     s->kind == AST_BREAK ? "break" : "continue");
        }
        break;
      default:
        break; /* 其他语句不创建作用域 */
    }
  }
  return r;
}

/* if 语句的作用域构建入口（含 else-if 同层递归） */
static build_result_t build_func_if(sema_t *sema, ast_if_t *it,
                                    sema_scope_t *scope) {
  sema_scope_t *then_scope =
      sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, scope);
  sema_scope_add_child(scope, then_scope);
  build_result_t tr =
      build_block(sema, (ast_block_t *)it->then_body, then_scope);
  build_result_t er = {0};
  if (it->else_body) {
    if (it->else_body->kind == AST_IF) {
      er = build_func_if(sema, (ast_if_t *)it->else_body, scope);
    } else {
      sema_scope_t *else_scope =
          sema_scope_new(sema->vm->alloc, SEMA_SCOPE_BLOCK, scope);
      sema_scope_add_child(scope, else_scope);
      er = build_block(sema, (ast_block_t *)it->else_body, else_scope);
    }
  }
  return (build_result_t){.definitely_returns =
                              tr.definitely_returns && er.definitely_returns};
}

void sema_build_scope_tree(sema_t *sema) {
  size_t n = vec_len(sema->funcs);
  for (size_t i = 0; i < n; i++) {
    build_func(sema, (sema_func_t *)vec_get(sema->funcs, i));
  }
}

/* ===========================================================================
 * Pass 3b：Shadow VM 运行
 *
 * 按预建作用域树严格对应遍历 AST。每个块用独立局部索引遍历自己的
 * children：build 阶段子作用域按出现顺序 add_child，walk 必须逐层
 * 独立取用（共享索引会在嵌套时污染外层，导致后续作用域错位）。
 * =========================================================================== */

typedef struct block_result {
  bool definitely_returns; /* 该块保证返回（所有路径都 return） */
} block_result_t;

static block_result_t walk_block(sema_t *sema, ast_node_t *block,
                                 sema_scope_t *scope, size_t *idx);
static block_result_t walk_stmt(sema_t *sema, ast_node_t *stmt,
                                sema_scope_t *scope, size_t *idx);
static block_result_t walk_if(sema_t *sema, ast_if_t *it, sema_scope_t *scope,
                              size_t *idx);

/* ---- 语句：变量定义 ---- */

/* strslice → NUL 终止临时缓冲（scope_define 内部复制 key，栈缓冲安全） */
static void name_to_cstr(strslice_t s, char *buf, size_t cap) {
  size_t n = s.len < cap - 1 ? s.len : cap - 1;
  memcpy(buf, s.ptr, n);
  buf[n] = '\0';
}

static void shadow_var_def(sema_t *sema, ast_var_def_t *vd,
                           sema_scope_t *scope) {
  sema_symbol_t *sym = sema_scope_find_local(scope, vd->name);
  if (!sym) return; /* 3a 重复定义已诊断，符号未注册 */

  value_t *var_value;
  if (vd->is_tdz) {
    /* TDZ：构造声明类型 shadow value 并标记 TDZ，定义到 VM scope。
       立即可见但不可读（只可赋值），由赋值退出 TDZ。 */
    var_value =
        value_make_shadow(sema->vm, sym->type ? sym->type : sema->vm->type_void);
    value_set_tdz(var_value, true);
  } else {
    /* 非 TDZ：先求值 init（定义尚未入 VM scope → 自引用解析到外层同名变量） */
    value_t *init = sema_expr(sema, vd->init, scope);
    bool init_bad = value_is_error(sema->vm, init) ||
                    type_eq(value_type(init), sema->vm->type_void);

    if (vd->type_name.len) {
      /* 显式类型：value_assign 校验 init 可赋给声明类型（单一校验点） */
      if (!init_bad && sym->type) {
        value_t *dst = value_make_shadow(sema->vm, sym->type);
        if (value_is_error(sema->vm, value_assign(sema->vm, dst, init))) {
          char tn[64], itn[64];
          sema_type_name(sym->type, tn, sizeof(tn));
          sema_type_name(value_type(init), itn, sizeof(itn));
          diag_error(sema->diag, sema_loc(sema, vd->init),
                     "cannot initialize variable '%.*s' of type %s with %s",
                     (int)vd->name.len, vd->name.ptr, tn, itn);
        }
      }
      var_value =
          value_make_shadow(sema->vm, sym->type ? sym->type : sema->vm->type_void);
    } else {
      /* 推断类型：init 类型即变量类型 */
      const type_t *vt = init_bad ? sema->vm->type_void : value_type(init);
      sym->type = vt;
      var_value = value_make_shadow(sema->vm, vt);
    }
  }

  /* 定义 shadow value 到当前 VM scope（与 sema scope 树同构；名字取自 ast） */
  char nb[256];
  name_to_cstr(vd->name, nb, sizeof nb);
  scope_define(sema->vm, sema->vm->current_scope, nb, var_value);
}

/* ---- 语句：赋值 ---- */

/* 复合赋值 token（+= 等）→ 基础二元运算 */
static value_t *(*compound_binop(const token_t *op))(vm_t *, value_t *,
                                                     value_t *) {
  if (token_is(op, "+=")) return value_add;
  if (token_is(op, "-=")) return value_sub;
  if (token_is(op, "*=")) return value_mul;
  if (token_is(op, "/=")) return value_div;
  if (token_is(op, "%=")) return value_mod;
  return NULL;
}

static void shadow_assign(sema_t *sema, ast_assign_t *as,
                          sema_scope_t *scope) {
  /* 显式丢弃：_ = expr（不查符号表，直接求值右值） */
  if (strslice_eq(as->name, STRSLICE_LIT("_"))) {
    if (!token_is(as->op, "=")) {
      diag_error(sema->diag, sema_loc(sema, &as->base),
                 "discard '_' only supports simple assignment '='");
    }
    sema_expr(sema, as->value, scope);
    return;
  }

  /* 左值从 VM scope 链 lookup（与 sema 作用域树同构） */
  value_t *lhs = scope_lookup(sema->vm->current_scope, as->name);
  if (!lhs) {
    diag_error(sema->diag, sema_loc(sema, &as->base),
               "undefined variable '%.*s' in assignment", (int)as->name.len,
               as->name.ptr);
    return;
  }

  value_t *rhs = sema_expr(sema, as->value, scope);
  bool rhs_bad = value_is_error(sema->vm, rhs) ||
                 type_eq(value_type(rhs), sema->vm->type_void);

  if (token_is(as->op, "=")) {
    if (rhs_bad) return; /* 错误恢复产物跳过，已有诊断 */
    /* 简单赋值：value_assign 校验（含 TDZ 变量——类型合法即退出 TDZ） */
    value_t *r = value_assign(sema->vm, lhs, rhs);
    if (value_is_error(sema->vm, r)) {
      char tn[64], rn[64];
      sema_type_name(value_type(lhs), tn, sizeof(tn));
      sema_type_name(value_type(rhs), rn, sizeof(rn));
      diag_error(sema->diag, sema_loc(sema, as->value),
                 "cannot assign %s to variable '%.*s' of type %s", rn,
                 (int)as->name.len, as->name.ptr, tn);
    } else {
      /* 赋值成功退出 TDZ */
      value_set_tdz(lhs, false);
    }
    return;
  }

  /* 复合赋值 x op= rhs → x = x op rhs（shadow 走 vtable 类型协商） */
  value_t *(*op)(vm_t *, value_t *, value_t *) = compound_binop(as->op);
  if (!op) {
    char ob[16];
    size_t len = 0;
    const char *text = as->op ? token_get_text(as->op, &len) : NULL;
    snprintf(ob, sizeof(ob), "%.*s", (int)len, text ? text : "?");
    diag_error(sema->diag, sema_loc(sema, &as->base),
               "unsupported compound assignment operator '%s'", ob);
    return;
  }
  value_t *result = op(sema->vm, lhs, rhs);
  if (value_is_error(sema->vm, result)) {
    char ob[16], tn[64], rn[64];
    size_t len = 0;
    const char *text = as->op ? token_get_text(as->op, &len) : NULL;
    snprintf(ob, sizeof(ob), "%.*s", (int)len, text ? text : "?");
    sema_type_name(value_type(lhs), tn, sizeof(tn));
    sema_type_name(value_type(rhs), rn, sizeof(rn));
    diag_error(sema->diag, sema_loc(sema, &as->base),
               "type mismatch: cannot apply '%s' to %s and %s", ob, tn, rn);
    return;
  }
  /* 复合赋值结果必须能赋回变量（value_assign 单一校验点） */
  if (value_is_error(sema->vm, value_assign(sema->vm, lhs, result))) {
    char tn[64], rn[64];
    sema_type_name(value_type(lhs), tn, sizeof(tn));
    sema_type_name(value_type(result), rn, sizeof(rn));
    diag_error(sema->diag, sema_loc(sema, &as->base),
               "cannot assign %s to variable '%.*s' of type %s", rn,
               (int)as->name.len, as->name.ptr, tn);
  }
}

/* ---- 语句：控制流 ---- */

static block_result_t walk_return(sema_t *sema, ast_return_t *rt,
                                  sema_scope_t *scope) {
  block_result_t r = {.definitely_returns = true};
  if (rt->value) {
    value_t *v = sema_expr(sema, rt->value, scope);
    bool v_bad = value_is_error(sema->vm, v) ||
                 type_eq(value_type(v), sema->vm->type_void);
    if (!v_bad) {
      if (!sema->func_return_type) {
        diag_error(sema->diag, sema_loc(sema, rt->value),
                   "void function cannot return a value");
      } else {
        /* value_assign 校验返回类型可赋给签名返回类型 */
        value_t *dst = value_make_shadow(sema->vm, sema->func_return_type);
        if (value_is_error(sema->vm, value_assign(sema->vm, dst, v))) {
          char tn[64], rn[64];
          sema_type_name(sema->func_return_type, tn, sizeof(tn));
          sema_type_name(value_type(v), rn, sizeof(rn));
          diag_error(sema->diag, sema_loc(sema, rt->value),
                     "cannot return %s from function returning %s", rn, tn);
        }
      }
    }
  } else {
    if (sema->func_return_type) {
      char tn[64];
      sema_type_name(sema->func_return_type, tn, sizeof(tn));
      diag_error(sema->diag, sema_loc(sema, &rt->base),
                 "function returning %s must return a value", tn);
    }
  }
  sema->func_has_return = true;
  return r;
}

static block_result_t walk_while(sema_t *sema, ast_while_t *wl,
                                 sema_scope_t *scope, size_t *idx) {
  value_t *cond = sema_expr(sema, wl->cond, scope);
  sema_check_bool(sema, wl->cond, cond, "while condition");

  sema_scope_t *body_scope = sema_scope_child(scope, (*idx)++);
  vm_push_scope(sema->vm); /* 循环体块：VM scope 与 sema scope 树同构 */
  size_t sub = 0;
  walk_block(sema, wl->body, body_scope ? body_scope : scope, &sub);
  vm_pop_scope(sema->vm);
  return (block_result_t){0}; /* 循环体可能不执行，不贡献 definitely_returns */
}

static block_result_t walk_for(sema_t *sema, ast_for_t *fr,
                               sema_scope_t *scope, size_t *idx) {
  sema_scope_t *for_scope = sema_scope_child(scope, (*idx)++);
  sema_scope_t *fs = for_scope ? for_scope : scope;

  vm_push_scope(sema->vm); /* for 作用域（init 变量） */

  /* init 在 for scope 内求值 */
  if (fr->init) {
    switch (fr->init->kind) {
      case AST_VAR_DEF:
        shadow_var_def(sema, (ast_var_def_t *)fr->init, fs);
        break;
      case AST_ASSIGN:
        shadow_assign(sema, (ast_assign_t *)fr->init, fs);
        break;
      case AST_EXPR_STMT:
        sema_expr(sema, ((ast_expr_stmt_t *)fr->init)->expr, fs);
        break;
      default:
        break;
    }
  }

  if (fr->cond) {
    value_t *c = sema_expr(sema, fr->cond, fs);
    sema_check_bool(sema, fr->cond, c, "for condition");
  }

  /* body 是 for scope 的子 scope（新局部迭代器） */
  size_t body_idx = 0;
  sema_scope_t *body_scope = sema_scope_child(for_scope, body_idx++);
  vm_push_scope(sema->vm); /* 循环体块 */
  size_t sub = 0;
  walk_block(sema, fr->body, body_scope ? body_scope : fs, &sub);
  vm_pop_scope(sema->vm);

  if (fr->update) sema_expr(sema, fr->update, fs);

  vm_pop_scope(sema->vm); /* 退出 for 作用域 */
  return (block_result_t){0};
}

static block_result_t walk_if(sema_t *sema, ast_if_t *it, sema_scope_t *scope,
                              size_t *idx) {
  block_result_t r = {0};
  value_t *cond = sema_expr(sema, it->cond, scope);
  sema_check_bool(sema, it->cond, cond, "if condition");

  block_result_t tr = {0};
  sema_scope_t *then_scope = sema_scope_child(scope, (*idx)++);
  vm_push_scope(sema->vm); /* then 块 */
  size_t sub = 0;
  tr = walk_block(sema, it->then_body, then_scope ? then_scope : scope, &sub);
  vm_pop_scope(sema->vm);

  block_result_t er = {0};
  if (it->else_body) {
    if (it->else_body->kind == AST_IF) {
      /* else-if 链：同层递归（子作用域顺序与 3a 一致：else-if 不单独
         建 scope，其 then 是当前 scope 的下一个子节点） */
      er = walk_if(sema, (ast_if_t *)it->else_body, scope, idx);
    } else {
      sema_scope_t *else_scope = sema_scope_child(scope, (*idx)++);
      vm_push_scope(sema->vm); /* else 块 */
      size_t sub2 = 0;
      er = walk_block(sema, it->else_body, else_scope ? else_scope : scope,
                      &sub2);
      vm_pop_scope(sema->vm);
    }
  }
  r.definitely_returns = tr.definitely_returns && er.definitely_returns;
  return r;
}

/* ---- 语句分派 ---- */

/* 索引语义：idx 是"当前 scope"的 children 迭代器。一个块内的语句按序
   消费当前 scope 的子节点（sema_scope_child(scope, (*idx)++)），进入
   嵌套子 scope（BLOCK/if then/else/while body/for body）时用新局部
   迭代器遍历子 scope 的 children——与原版共享外层迭代器相比，嵌套消费
   不再污染浅层（第二个 for 的 init 变量因此找不到定义点）。 */

static block_result_t walk_stmt(sema_t *sema, ast_node_t *stmt,
                                sema_scope_t *scope, size_t *idx) {
  block_result_t r = {0};
  switch (stmt->kind) {
    case AST_VAR_DEF:
      shadow_var_def(sema, (ast_var_def_t *)stmt, scope);
      break;
    case AST_ASSIGN:
      shadow_assign(sema, (ast_assign_t *)stmt, scope);
      break;
    case AST_BLOCK: {
      sema_scope_t *child = sema_scope_child(scope, (*idx)++);
      vm_push_scope(sema->vm); /* VM scope 与 sema scope 树同构 */
      size_t sub = 0;
      r = walk_block(sema, stmt, child ? child : scope, &sub);
      vm_pop_scope(sema->vm);
      break;
    }
    case AST_IF:
      r = walk_if(sema, (ast_if_t *)stmt, scope, idx);
      break;
    case AST_WHILE:
      r = walk_while(sema, (ast_while_t *)stmt, scope, idx);
      break;
    case AST_FOR:
      r = walk_for(sema, (ast_for_t *)stmt, scope, idx);
      break;
    case AST_RETURN:
      r = walk_return(sema, (ast_return_t *)stmt, scope);
      break;
    case AST_BREAK:
    case AST_CONTINUE:
      break; /* 位置检查已在 Pass 3a 完成 */
    case AST_EXPR_STMT: {
      ast_expr_stmt_t *es = (ast_expr_stmt_t *)stmt;
      value_t *v = sema_expr(sema, es->expr, scope);
      if (!value_is_error(sema->vm, v) &&
          !type_eq(value_type(v), sema->vm->type_void)) {
        char tn[64];
        sema_type_name(value_type(v), tn, sizeof(tn));
        diag_error(sema->diag, sema_loc(sema, es->expr),
                   "expression result of type %s is unused; use '_ = expr' "
                   "to discard",
                   tn);
      }
      break;
    }
    default:
      break;
  }
  return r;
}

static block_result_t walk_block(sema_t *sema, ast_node_t *block,
                                 sema_scope_t *scope, size_t *idx) {
  block_result_t r = {0};
  ast_block_t *b = (ast_block_t *)block;
  for (ast_node_t *s = b->stmts; s; s = s->next) {
    block_result_t sr = walk_stmt(sema, s, scope, idx);
    if (sr.definitely_returns) {
      r.definitely_returns = true;
      break; /* 之后的语句不可达，不再检查 */
    }
  }
  return r;
}

/* ---- 函数入口 ---- */

void sema_walk_function(sema_t *sema, sema_func_t *sf) {
  ast_func_def_t *fn = (ast_func_def_t *)sf->def;
  if (!sf->scope) return; /* 建树失败（结构错误已诊断），不进入 shadow run */

  sema->func_return_type =
      fn->return_type.len ? resolve_type(sema, fn->return_type) : NULL;
  sema->func_has_return = false;

  /* 函数级 VM scope（与 fscope 同构）：参数 shadow value 定义到此处，
     进入函数体即可读（名字取自 ast） */
  vm_push_scope(sema->vm);
  for (ast_node_t *p = fn->params; p; p = p->next) {
    ast_var_def_t *vd = (ast_var_def_t *)p;
    sema_symbol_t *ps = sema_scope_find_local(sf->scope, vd->name);
    value_t *pv = value_make_shadow(sema->vm,
                                    ps && ps->type ? ps->type
                                                   : sema->vm->type_void);
    char nb[256];
    name_to_cstr(vd->name, nb, sizeof nb);
    scope_define(sema->vm, sema->vm->current_scope, nb, pv);
  }

  /* 返回路径完整性分析已在 Pass 3a（建树阶段）完成；
     walk_block 的 block_result_t 仅用于跳过不可达语句的类型检查 */
  size_t child_idx = 0;
  (void)walk_block(sema, fn->body, sf->scope, &child_idx);
  vm_pop_scope(sema->vm);

  /* 返回路径完整性分析已在 Pass 3a（建树阶段）完成 */
  sema->func_return_type = NULL;
}
