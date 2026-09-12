#include "sema/sema.h"
#include "vm/type_func.h"
#include "core/panic.h"
#include "core/string.h"
#include "parser/ast_const.h"
#include "parser/ast_func_def.h"
#include "parser/ast_ident.h"
#include "parser/ast_program.h"
#include "parser/ast_var_def.h"
#include "parser/ast_volatile.h"
#include "parser/lexer.h"
#include "sema/comptime.h"
#include <string.h>

/* ===========================================================================
 * 上下文管理
 * =========================================================================== */

sema_t *sema_create(vm_t *vm, diag_buf_t *diag, vec_t *tokens,
                    arena_t *arena) {
  if (!vm || !diag || !tokens || !arena) return NULL;
  sema_t *sema =
      allocator_new_ex(vm->alloc, "sema_t", sizeof(sema_t), NULL, NULL, NULL,
                       1);
  sema->vm = vm;
  sema->diag = diag;
  sema->tokens = tokens;
  sema->arena = arena;
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

const type_t *resolve_type_expr(sema_t *sema, ast_node_t *type_expr) {
  if (!sema || !type_expr) return NULL;

  switch (type_expr->kind) {
    case AST_IDENT: {
      /* 类型名引用：i32/bool 等关键字类型名与用户类型名（IDENTIFIER）
         统一为 AST_IDENT（类型即表达式），查类型表。 */
      ast_ident_t *id = (ast_ident_t *)type_expr;
      return type_lookup(sema->vm, id->name);
    }
    case AST_CONST: {
      /* const 类型修饰：嵌套递归（const const T 收敛为 const T） */
      const type_t *sub =
          resolve_type_expr(sema, ((ast_const_t *)type_expr)->sub);
      return sub ? type_const_intern(sema->vm, sub) : NULL;
    }
    case AST_VOLATILE: {
      const type_t *sub =
          resolve_type_expr(sema, ((ast_volatile_t *)type_expr)->sub);
      return sub ? type_volatile_intern(sema->vm, sub) : NULL;
    }
    default:
      /* M2 扩展点：数组/元组/func 类型表达式 + 类型计算 */
      diag_error(sema->diag, sema_loc(sema, type_expr),
                 "unsupported type expression");
      return NULL;
  }
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

/* ===========================================================================
 * Pass 1/2：函数名收集 + 类型解析（func_t 签名）
 * =========================================================================== */

static void pass1_names(sema_t *sema, ast_program_t *prog) {
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    /* 全局 comptime var：注册变量符号（暂不激活，pass_globals 求值后激活）。
       不创建 sema_func_t（不入 Pass 3 队列——不是函数，无函数体）。 */
    if (f->kind == AST_VAR_DEF) {
      ast_var_def_t *vd = (ast_var_def_t *)f;
      sema_symbol_t init = {0};
      sema_symbol_t *sym =
          sema_scope_define(sema->global_scope, vd->name, &init);
      if (!sym) {
        diag_error(sema->diag, sema_loc(sema, f), "duplicate name '%.*s'",
                   (int)vd->name.len, vd->name.ptr);
      }
      continue;
    }

    ast_func_def_t *fn = (ast_func_def_t *)f;
    sema_symbol_t init = {0}; /* 函数定义顺序自由：Pass 1 全部注册，无遮罩问题 */
    sema_symbol_t *sym = sema_scope_define(sema->global_scope, fn->name, &init);
    if (!sym) {
      diag_error(sema->diag, sema_loc(sema, f), "duplicate function '%.*s'",
                 (int)fn->name.len, fn->name.ptr);
      continue; /* 重复定义不入队 */
    }
    /* 函数名全局可见（无 TDZ）→ 注册即激活 */
    sym->is_active = true;
    sym->is_comptime = fn->is_comptime;
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
        const type_t *t = resolve_type_expr(sema, vd->type_expr);
        if (!t) {
          diag_error(sema->diag, sema_loc(sema, p),
                     "unknown type in parameter '%.*s'",
                     (int)vd->name.len, vd->name.ptr);
        }
        params[j] = t; /* 失败置 NULL，位置对齐，func_shadow_call 校验时跳过 */
      }
    }

    const type_t *rt = NULL;
    if (fn->return_expr) {
      rt = resolve_type_expr(sema, fn->return_expr);
      if (!rt) {
        diag_error(sema->diag, sema_loc(sema, f),
                   "unknown return type");
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
 * pass_globals：全局 comptime var 求值
 *
 * 遍历 prog->funcs 链上的 AST_VAR_DEF（parser 仅把 comptime var 挂上链），
 * 逐一定义点求值（sema_eval_comptime_var：改写 init → ctfe 求值 → 符号表
 * 编码）。无论成功失败都从链摘除——comptime var 不进入运行时。
 * 在 pass2_types 之后执行：函数签名已解析，init 可调用函数（含 comptime func）。
 * =========================================================================== */

static void pass_globals(sema_t *sema, ast_program_t *prog) {
  ast_node_t **prev = &prog->funcs;
  for (ast_node_t *f = prog->funcs; f;) {
    ast_node_t *next = f->next;
    if (f->kind == AST_VAR_DEF) {
      /* 失败（诊断已记录）：仍摘除，避免下游误以为全局 var 存在 */
      sema_eval_comptime_var(sema, (ast_var_def_t *)f, sema->global_scope);
      *prev = next;
    } else {
      prev = &f->next;
    }
    f = next;
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

  /* 预注册内置函数符号（printf：variadic 签名，ast=NULL 表示无 AST 定义）。
     与 VM 侧 vm_register_printf 对应；签名类型经 type_func_sig intern。 */
  {
    const type_t *pparams[1] = { sema->vm->type_str };
    const type_t *psig = type_func_sig(sema->vm, pparams, 1, NULL,
                                       /*is_variadic=*/true);
    sema_symbol_t init = {.type = psig, .ast = NULL, .is_active = true};
    sema_scope_define(sema->global_scope, STRSLICE_LIT("printf"), &init);
  }

  pass1_names(sema, prog);
  pass2_types(sema);
  pass_globals(sema, prog);

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
    sema_func_t *sf = (sema_func_t *)vec_get(sema->funcs, i);
    ast_func_def_t *fn = (ast_func_def_t *)sf->def;
    /* comptime func：调用点折叠为字面量（sema_eval_comptime_call），
       不 shadow walk（参数为 shadow 无法编译期求值，body 求值在调用点）。 */
    if (fn->is_comptime) continue;
    sema_walk_function(sema, sf);
  }

  return !diag_has_error(sema->diag);
}
