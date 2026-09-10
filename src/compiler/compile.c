#include "compiler/compiler.h"
#include "core/panic.h"
#include "core/vec.h"
#include "parser/ast_program.h"
#include "parser/ast_func_def.h"
#include "parser/ast_var_def.h"
#include "parser/ast_block.h"
#include "parser/ast_if.h"
#include "parser/ast_while.h"
#include "parser/ast_for.h"
#include "parser/ast_return.h"
#include "parser/ast_assign.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_ident.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_binary.h"
#include "parser/ast_unary.h"
#include "parser/ast_call.h"
#include "parser/ast_cast.h"
#include "parser/lexer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ===========================================================================
 * 内部工具
 * =========================================================================== */

static class_t g_compiler_class = {
    .name       = "clux.compiler",
    .size       = sizeof(compiler_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

static class_t g_patch_class = {
    .name       = "clux.compiler.patch",
    .size       = sizeof(compile_patch_t),
    .clone_fn   = NULL,
    .move_fn    = NULL,
    .dispose_fn = NULL,
};

static location_t c_loc(compiler_t *c, const ast_node_t *node) {
  location_t zero = {0};
  if (!c || !node) return zero;
  const token_t *t = (const token_t *)vec_get(c->tokens, node->tok_begin);
  const location_t *loc = t ? token_get_location(t) : NULL;
  return loc ? *loc : zero;
}

static void c_error(compiler_t *c, const ast_node_t *node, const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  diag_error(c->diag, c_loc(c, node), "%s", buf);
  c->failed = true;
}

/* ---- 标签工具 ---- */

static void label_init(compile_label_t *l) {
  l->defined = false;
  l->pos     = 0;
  l->patches = NULL;
}

/** 标记标签当前位置（后续跳转到此的 patch 回填此处） */
static void label_here(compiler_t *c, compile_label_t *l) {
  l->defined = true;
  l->pos     = bcode_tell(c->bc);
  /* 回填所有前向 patch */
  compile_patch_t *p = l->patches;
  while (p) {
    bcode_patch_u32(c->bc, p->pos, (uint32_t)l->pos);
    compile_patch_t *next = p->next;
    allocator_free(c->alloc, (void **)&p);
    p = next;
  }
  l->patches = NULL;
}

/**
 * 发射跳转到 label：label 已定义 → 直接写目标；未定义 → 占位 0 并记 patch。
 * 跳转指令的 opcode 由调用方先发，操作数字段在 tell 时（opcode+4）取样。
 */
static void emit_jump(compiler_t *c, compile_label_t *l) {
  size_t pos = bcode_tell(c->bc); /* opcode 已由调用方发出，此处即操作数字段 */
  if (l->defined) {
    bcode_write_u32(c->bc, (uint32_t)l->pos);
    return;
  }
  bcode_write_u32(c->bc, 0); /* 前向占位 */
  compile_patch_t *p = (compile_patch_t *)allocator_new(c->alloc, &g_patch_class, 1);
  if (!p) panic("compiler: out of memory allocating patch");
  memset(p, 0, sizeof(*p));
  p->pos = pos;
  p->next = l->patches;
  l->patches = p;}

/* ---- 静态平衡工具 ---- */

static void balance_push(compiler_t *c) {
  c->scope_depth++;
  bcode_write_op(c->bc, BCODE_PUSH_SCOPE);
}

static void balance_pop(compiler_t *c) {
  if (c->scope_depth == 0) {
    c_error(c, NULL, "compiler: internal scope imbalance");
    return;
  }
  c->scope_depth--;
  bcode_write_op(c->bc, BCODE_POP_SCOPE);
}

/* 为跳转平衡作用域：跨出 n 层块作用域先发 n 个 POP_SCOPE。
   仅发指令、不改编译期 scope_depth——跳转（break/continue）是控制流出口，
   运行时在跳转前弹出 n 层；编译期静态深度仍保持当前块结构，后续语句
   （含 break/continue 之后的 dead code）继续按原嵌套深度编译，块级
   balance_pop 照常归位，避免双重弹出导致 imbalance。 */
static void balance_scopes_out(compiler_t *c, size_t n) {
  while (n-- > 0) bcode_write_op(c->bc, BCODE_POP_SCOPE);
}

/* ---- 栈深度静态追踪 ---- */

static void st_push(compiler_t *c, int delta) { c->stack_depth += delta; }

/* ===========================================================================
 * 前向声明
 * =========================================================================== */

static void compile_stmt(compiler_t *c, ast_node_t *node);
static void compile_expr(compiler_t *c, ast_node_t *node);
static void compile_type_expr(compiler_t *c, strslice_t type_name);

/* ===========================================================================
 * 类型表达式（M1：仅 identifier 一种形式 → LOAD "name"）
 * =========================================================================== */

static void compile_type_expr(compiler_t *c, strslice_t type_name) {
  if (strslice_is_empty(type_name)) {
    /* 空类型：PUSH_UNDEFINED（"待推导"占位，op_define 从值推断） */
    bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED);
    st_push(c, 1);
    return;
  }
  /* 类型当作表达式：LOAD 从 global scope 查 type value 压栈 */
  bcode_write_op(c->bc, BCODE_LOAD);
  bcode_write_str(c->bc, type_name);
  st_push(c, 1);
}

/* ===========================================================================
 * 表达式节点
 * =========================================================================== */

static void compile_expr(compiler_t *c, ast_node_t *node) {
  if (!node || c->failed) return;
  switch (node->kind) {
  case AST_INT_LIT: {
    ast_int_lit_t *n = (ast_int_lit_t *)node;
    /* 默认 i32；带后缀 → 按后缀宽度发射对应 PUSH_* */
    if (strslice_is_empty(n->type)) {
      bcode_write_op(c->bc, BCODE_PUSH_I32);
      bcode_write_i32(c->bc, (int32_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("i8"))) {
      bcode_write_op(c->bc, BCODE_PUSH_I8);  bcode_write_i8(c->bc, (int8_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("i16"))) {
      bcode_write_op(c->bc, BCODE_PUSH_I16); bcode_write_i16(c->bc, (int16_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("i32"))) {
      bcode_write_op(c->bc, BCODE_PUSH_I32); bcode_write_i32(c->bc, (int32_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("i64"))) {
      bcode_write_op(c->bc, BCODE_PUSH_I64); bcode_write_i64(c->bc, (int64_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("u8"))) {
      bcode_write_op(c->bc, BCODE_PUSH_U8);  bcode_write_u8(c->bc, (uint8_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("u16"))) {
      bcode_write_op(c->bc, BCODE_PUSH_U16); bcode_write_u16(c->bc, (uint16_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("u32"))) {
      bcode_write_op(c->bc, BCODE_PUSH_U32); bcode_write_u32(c->bc, (uint32_t)n->value);
    } else if (strslice_eq(n->type, STRSLICE_LIT("u64"))) {
      bcode_write_op(c->bc, BCODE_PUSH_U64); bcode_write_u64(c->bc, (uint64_t)n->value);
    } else {
      c_error(c, node, "unsupported integer literal type '%.*s'",
              (int)n->type.len, n->type.ptr);
      return;
    }
    st_push(c, 1);
    break;
  }
  case AST_FLOAT_LIT: {
    ast_float_lit_t *n = (ast_float_lit_t *)node;
    if (strslice_eq(n->type, STRSLICE_LIT("f32"))) {
      bcode_write_op(c->bc, BCODE_PUSH_F32); bcode_write_f32(c->bc, (float)n->value);
    } else if (strslice_is_empty(n->type) || strslice_eq(n->type, STRSLICE_LIT("f64"))) {
      bcode_write_op(c->bc, BCODE_PUSH_F64); bcode_write_f64(c->bc, n->value);
    } else {
      c_error(c, node, "unsupported float literal type '%.*s'",
              (int)n->type.len, n->type.ptr);
      return;
    }
    st_push(c, 1);
    break;
  }
  case AST_BOOL_LIT: {
    ast_bool_lit_t *n = (ast_bool_lit_t *)node;
    bcode_write_op(c->bc, BCODE_PUSH_BOOL); bcode_write_bool(c->bc, n->value);
    st_push(c, 1);
    break;
  }
  case AST_CHAR_LIT: {
    ast_char_lit_t *n = (ast_char_lit_t *)node;
    bcode_write_op(c->bc, BCODE_PUSH_U8); bcode_write_u8(c->bc, (uint8_t)n->value);
    st_push(c, 1);
    break;
  }
  case AST_STRING_LIT: {
    ast_string_lit_t *n = (ast_string_lit_t *)node;
    bcode_write_op(c->bc, BCODE_PUSH_STR); bcode_write_str(c->bc, n->text);
    st_push(c, 1);
    break;
  }
  case AST_IDENT: {
    ast_ident_t *n = (ast_ident_t *)node;
    bcode_write_op(c->bc, BCODE_PUSH); bcode_write_str(c->bc, n->name);
    st_push(c, 1);
    break;
  }
  case AST_BINARY: {
    ast_binary_t *n = (ast_binary_t *)node;
    if (token_is(n->op, "&&") || token_is(n->op, "||")) {
      /* 短路编译（结果恒在栈上）：
         a && b  →  lhs; JZ L_false; rhs; JMP L_end; L_false: PUSH_BOOL false; L_end:
         a || b  →  lhs; JNZ L_true;  rhs; JMP L_end; L_true:  PUSH_BOOL true;  L_end: */
      bool is_and = token_is(n->op, "&&");
      compile_expr(c, n->lhs);            /* 栈: [lhs] */
      compile_label_t short_out, short_skip;
      label_init(&short_out);
      label_init(&short_skip);
      bcode_write_op(c->bc, is_and ? BCODE_JZ : BCODE_JNZ);
      emit_jump(c, &short_out);           /* 弹 lhs，短路跳 */
      compile_expr(c, n->rhs);            /* 栈: [rhs] */
      bcode_write_op(c->bc, BCODE_JMP);
      emit_jump(c, &short_skip);
      label_here(c, &short_out);
      bcode_write_op(c->bc, BCODE_PUSH_BOOL);
      bcode_write_bool(c->bc, is_and ? false : true);  /* 栈: [短路常量] */
      label_here(c, &short_skip);
      /* 结果保留在栈上，栈深不变（压 1 净 +1 与普通二元一致） */
      st_push(c, 1);
      break;
    }
    /* 常规二元：lhs → rhs → op */
    compile_expr(c, n->lhs);
    compile_expr(c, n->rhs);
    if (token_is(n->op, "+"))  bcode_write_op(c->bc, BCODE_ADD);
    else if (token_is(n->op, "-"))  bcode_write_op(c->bc, BCODE_SUB);
    else if (token_is(n->op, "*"))  bcode_write_op(c->bc, BCODE_MUL);
    else if (token_is(n->op, "/"))  bcode_write_op(c->bc, BCODE_DIV);
    else if (token_is(n->op, "%"))  bcode_write_op(c->bc, BCODE_MOD);
    else if (token_is(n->op, "==")) bcode_write_op(c->bc, BCODE_EQ);
    else if (token_is(n->op, "!=")) bcode_write_op(c->bc, BCODE_NE);
    else if (token_is(n->op, "<"))  bcode_write_op(c->bc, BCODE_LT);
    else if (token_is(n->op, "<=")) bcode_write_op(c->bc, BCODE_LE);
    else if (token_is(n->op, ">"))  bcode_write_op(c->bc, BCODE_GT);
    else if (token_is(n->op, ">=")) bcode_write_op(c->bc, BCODE_GE);
    else if (token_is(n->op, "&"))  bcode_write_op(c->bc, BCODE_AND);
    else if (token_is(n->op, "|"))  bcode_write_op(c->bc, BCODE_OR);
    else if (token_is(n->op, "^"))  bcode_write_op(c->bc, BCODE_BXOR);
    else if (token_is(n->op, "<<")) bcode_write_op(c->bc, BCODE_SHL);
    else if (token_is(n->op, ">>")) bcode_write_op(c->bc, BCODE_SHR);
    else {
      c_error(c, node, "unsupported binary operator");
      return;
    }
    st_push(c, -1); /* 两弹一压 */
    break;
  }
  case AST_UNARY: {
    ast_unary_t *n = (ast_unary_t *)node;
    compile_expr(c, n->operand);
    if (token_is(n->op, "-"))       bcode_write_op(c->bc, BCODE_NEG);
    else if (token_is(n->op, "!"))  bcode_write_op(c->bc, BCODE_NOT);
    else if (token_is(n->op, "~"))  bcode_write_op(c->bc, BCODE_BNOT);
    else {
      c_error(c, node, "unsupported unary operator");
      return;
    }
    break;
  }
  case AST_CALL: {
    ast_call_t *n = (ast_call_t *)node;
    compile_expr(c, n->callee);     /* 栈: [callee] */
    size_t argc = 0;
    for (ast_node_t *a = n->args; a; a = a->next) {
      compile_expr(c, a);           /* 栈: [callee, arg1..argN] */
      argc++;
    }
    bcode_write_op(c->bc, BCODE_CALL);
    bcode_write_u32(c->bc, (uint32_t)argc);
    st_push(c, -((int)argc));       /* callee+args 弹出，结果压入 */
    break;
  }
  case AST_CAST: {
    ast_cast_t *n = (ast_cast_t *)node;
    compile_expr(c, n->expr);        /* 栈: [value] */
    compile_type_expr(c, n->target_type); /* 栈: [value, type] */
    bcode_write_op(c->bc, BCODE_CAST);    /* 弹 type + value → 结果 */
    st_push(c, -1);
    break;
  }
  case AST_MEMBER:
    c_error(c, node, "member access is not supported in M1");
    return;
  case AST_INDEX:
    c_error(c, node, "index expression is not supported in M1");
    return;
  default:
    c_error(c, node, "unsupported expression node '%s'", ast_kind_name(node->kind));
    return;
  }
}

/* ===========================================================================
 * 语句节点
 * =========================================================================== */

/* 语句编译：语句产生的栈上值由语句自身平衡（压栈 → 使用 → 清理） */

static void compile_stmt(compiler_t *c, ast_node_t *node) {
  if (!node || c->failed) return;
  switch (node->kind) {
  case AST_VAR_DEF: {
    ast_var_def_t *n = (ast_var_def_t *)node;
    if (n->init) {
      compile_expr(c, n->init);              /* 栈: [value] */
    } else {
      bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED); /* 无初始值 → TDZ */
      st_push(c, 1);
    }
    if (!strslice_is_empty(n->type_name)) {
      compile_type_expr(c, n->type_name);    /* 栈: [value, type] */
    }
    bcode_write_op(c->bc, BCODE_DEFINE);
    bcode_write_str(c->bc, n->name);
    /* DEFINE 弹掉全部（双弹或单弹），栈深归零 */
    st_push(c, -2);
    break;
  }
  case AST_ASSIGN: {
    ast_assign_t *n = (ast_assign_t *)node;
    if (token_is(n->op, "=") && strslice_eq(n->name, STRSLICE_LIT("_"))) {
      /* 显式丢弃：_ = expr → 只求值右值并 POP（不 STORE，_ 不是变量）。
         sema 已校验 op 必须是 '='。 */
      compile_expr(c, n->value);               /* 栈: [value] */
      bcode_write_op(c->bc, BCODE_POP);        /* 丢弃结果 */
      st_push(c, -1);
      break;
    }
    if (token_is(n->op, "=")) {
      /* 直接赋值：value → STORE name */
      compile_expr(c, n->value);               /* 栈: [value] */
      bcode_write_op(c->bc, BCODE_STORE);
      bcode_write_str(c->bc, n->name);         /* STORE 压回结果，栈: [result] */
      bcode_write_op(c->bc, BCODE_POP);        /* 赋值是语句：丢弃结果 */
      st_push(c, -1);
    } else {
      /* 复合赋值 name op= v → PUSH name; v; op; STORE name（C 语义：
         x += v 等价于 x = x + v；栈序 [old, v] 保证 BINARY_OP
         弹 b=v、弹 a=old → value_fn(old, v)） */
      bcode_write_op(c->bc, BCODE_PUSH);
      bcode_write_str(c->bc, n->name);       /* 栈: [old] */
      compile_expr(c, n->value);             /* 栈: [old, v] */
      if (token_is(n->op, "+="))      bcode_write_op(c->bc, BCODE_ADD);
      else if (token_is(n->op, "-=")) bcode_write_op(c->bc, BCODE_SUB);
      else if (token_is(n->op, "*=")) bcode_write_op(c->bc, BCODE_MUL);
      else if (token_is(n->op, "/=")) bcode_write_op(c->bc, BCODE_DIV);
      else if (token_is(n->op, "%=")) bcode_write_op(c->bc, BCODE_MOD);
      else { c_error(c, node, "unsupported compound assignment"); return; }
      /* 栈: [result] */
      bcode_write_op(c->bc, BCODE_STORE);
      bcode_write_str(c->bc, n->name);       /* 压回结果，栈: [result] */
      bcode_write_op(c->bc, BCODE_POP);      /* 赋值是语句：丢弃结果 */
      st_push(c, -2);
    }
    break;
  }
  case AST_EXPR_STMT: {
    ast_expr_stmt_t *n = (ast_expr_stmt_t *)node;
    compile_expr(c, n->expr);                /* 栈: [value] */
    bcode_write_op(c->bc, BCODE_POP);        /* 丢弃结果 */
    st_push(c, -1);
    break;
  }
  case AST_RETURN: {
    ast_return_t *n = (ast_return_t *)node;
    if (n->value) {
      compile_expr(c, n->value);             /* 栈: [retval] */
    } else {
      bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED); /* void return */
      st_push(c, 1);
    }
    bcode_write_op(c->bc, BCODE_RET);        /* 栈顶即返回值 */
    st_push(c, -1);                          /* 返回值被 RET 消费 */
    break;
  }
  case AST_BLOCK: {
    ast_block_t *n = (ast_block_t *)node;
    balance_push(c);
    for (ast_node_t *s = n->stmts; s; s = s->next) compile_stmt(c, s);
    balance_pop(c);
    break;
  }
  case AST_IF: {
    ast_if_t *n = (ast_if_t *)node;
    compile_expr(c, n->cond);                /* 栈: [cond] */
    compile_label_t else_l, end_l;
    label_init(&else_l);
    label_init(&end_l);
    bcode_write_op(c->bc, BCODE_JZ);
    emit_jump(c, &else_l);                   /* 栈: [] */

    /* then */
    if (n->then_body->kind == AST_BLOCK) {
      balance_push(c);
      ast_block_t *b = (ast_block_t *)n->then_body;
      for (ast_node_t *s = b->stmts; s; s = s->next) compile_stmt(c, s);
      balance_pop(c);
    } else {
      compile_stmt(c, n->then_body);
    }
    if (n->else_body) {
      bcode_write_op(c->bc, BCODE_JMP);
      emit_jump(c, &end_l);
    }
    label_here(c, &else_l);

    /* else */
    if (n->else_body) {
      if (n->else_body->kind == AST_BLOCK) {
        balance_push(c);
        ast_block_t *b = (ast_block_t *)n->else_body;
        for (ast_node_t *s = b->stmts; s; s = s->next) compile_stmt(c, s);
        balance_pop(c);
      } else {
        compile_stmt(c, n->else_body);
      }
      label_here(c, &end_l);
    }
    break;
  }
  case AST_WHILE: {
    ast_while_t *n = (ast_while_t *)node;

    compile_label_t loop_top, loop_end;
    label_init(&loop_top);
    label_init(&loop_end);

    /* 循环上下文（break→end，continue→top）。label 存指针：
       循环体编译期间 emit_jump 直接读写同一 label（后向跳转已 defined
       时直接写目标，前向跳转共享 patches 列表统一回填） */
    compile_loop_t lc;
    lc.break_label = &loop_end;
    lc.continue_label = &loop_top;
    lc.scope_depth = c->scope_depth;
    lc.next = c->loop_stack;
    c->loop_stack = &lc;

    label_here(c, &loop_top);                /* 循环顶：条件 */
    compile_expr(c, n->cond);                /* 栈: [cond] */
    bcode_write_op(c->bc, BCODE_JZ);
    emit_jump(c, &loop_end);                 /* 栈: [] */

    balance_push(c);                         /* 循环体块作用域 */
    ast_block_t *b = (ast_block_t *)n->body;
    for (ast_node_t *s = b->stmts; s; s = s->next) compile_stmt(c, s);
    balance_pop(c);

    bcode_write_op(c->bc, BCODE_JMP);
    emit_jump(c, &loop_top);
    label_here(c, &loop_end);

    c->loop_stack = lc.next;
    break;
  }
  case AST_FOR: {
    ast_for_t *n = (ast_for_t *)node;

    balance_push(c);                         /* for 作用域（init 变量） */
    if (n->init) compile_stmt(c, n->init);

    compile_label_t loop_top, loop_end, loop_cont;
    label_init(&loop_top);
    label_init(&loop_end);
    label_init(&loop_cont);

    compile_loop_t lc;
    lc.break_label = &loop_end;
    lc.continue_label = &loop_cont;          /* for 的 continue → update 段 */
    lc.scope_depth = c->scope_depth;
    lc.next = c->loop_stack;
    c->loop_stack = &lc;

    label_here(c, &loop_top);                /* 循环顶：条件 */
    if (n->cond) {
      compile_expr(c, n->cond);              /* 栈: [cond] */
      bcode_write_op(c->bc, BCODE_JZ);
      emit_jump(c, &loop_end);               /* 栈: [] */
    }

    /* 循环体块作用域 */
    balance_push(c);
    ast_block_t *b = (ast_block_t *)n->body;
    for (ast_node_t *s = b->stmts; s; s = s->next) compile_stmt(c, s);
    balance_pop(c);

    label_here(c, &loop_cont);               /* continue 目标：update 段 */
    if (n->update) {
      if (n->update->kind == AST_ASSIGN || n->update->kind == AST_EXPR_STMT) {
        compile_stmt(c, n->update);
      } else {
        compile_expr(c, n->update);
        bcode_write_op(c->bc, BCODE_POP);
        st_push(c, -1);
      }
    }
    bcode_write_op(c->bc, BCODE_JMP);
    emit_jump(c, &loop_top);
    label_here(c, &loop_end);

    c->loop_stack = lc.next;
    balance_pop(c);                          /* 退出 for 作用域 */
    break;
  }
  case AST_BREAK: {
    if (!c->loop_stack) { c_error(c, node, "break outside loop"); return; }
    /* 跳出到循环出口：先平衡当前块作用域到循环基准深度 */
    size_t out = c->scope_depth - c->loop_stack->scope_depth;
    balance_scopes_out(c, out);
    bcode_write_op(c->bc, BCODE_JMP);
    emit_jump(c, c->loop_stack->break_label);
    break;
  }
  case AST_CONTINUE: {
    if (!c->loop_stack) { c_error(c, node, "continue outside loop"); return; }
    size_t out = c->scope_depth - c->loop_stack->scope_depth;
    balance_scopes_out(c, out);
    bcode_write_op(c->bc, BCODE_JMP);
    emit_jump(c, c->loop_stack->continue_label);
    break;
  }
  case AST_ERROR:
    c_error(c, node, "compile aborted on parse error node");
    return;
  default:
    c_error(c, node, "unsupported statement node '%s'", ast_kind_name(node->kind));
    return;
  }
}

/* ===========================================================================
 * 函数节点 + 程序
 * =========================================================================== */

/**
 * 编译单个函数体：
 *  - 倒序 DEFINE 绑参（参数定义到 func_vcall 推入的匿名局部作用域）
 *  - DEFINE 完成后 PUSH_SCOPE：函数体临时变量/块内变量定义到独立子作用域，
 *    与参数隔离（同名不冲突，且临时变量随函数返回销毁）
 *  - body 语句 → POP_SCOPE → return 兜底
 * 返回函数体入口（entry_pc），供注册段 PUSH_FUNCTION 引用。
 */
static size_t compile_func_body(compiler_t *c, ast_func_def_t *fn) {
  size_t body = bcode_tell(c->bc);

  /* 倒序绑定参数：DEFINE name（实参已按序压栈，栈顶 = 最后一个参数） */
  /* 收集参数名（倒序） */
  strslice_t names[64];
  size_t ni = 0;
  for (ast_node_t *p = fn->params; p; p = p->next) {
    ast_var_def_t *vd = (ast_var_def_t *)p;
    if (ni < sizeof(names) / sizeof(names[0])) names[ni++] = vd->name;
  }
  for (size_t i = ni; i-- > 0; ) {
    bcode_write_op(c->bc, BCODE_DEFINE);
    bcode_write_str(c->bc, names[i]);
  }

  /* 参数绑定完成 → push 函数体作用域（临时变量与参数隔离，遮蔽语义正确） */
  balance_push(c);

  /* 函数体语句（顶层变量定义到新作用域） */
  if (fn->body && fn->body->kind == AST_BLOCK) {
    ast_block_t *b = (ast_block_t *)fn->body;
    for (ast_node_t *s = b->stmts; s; s = s->next) compile_stmt(c, s);
  }

  balance_pop(c);

  /* return 兜底：无显式 return 时压 undefined + RET */
  bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED);
  bcode_write_op(c->bc, BCODE_RET);
  return body;
}

/** 编译函数注册段（JMP 守卫之后）：签名构造 + PUSH_FUNCTION + DEFINE_FUNCTION */
static void compile_func_reg(compiler_t *c, ast_func_def_t *fn, size_t body) {
  /* 签名弹栈顺序：[return, param1..argc, is_variadic] */
  /* 1. return 类型 */
  if (strslice_is_empty(fn->return_type)) {
    bcode_write_op(c->bc, BCODE_LOAD);
    bcode_write_str(c->bc, STRSLICE_LIT("void"));
    st_push(c, 1);
  } else {
    compile_type_expr(c, fn->return_type);
  }
  /* 2. 参数类型（按声明顺序） */
  size_t argc = 0;
  for (ast_node_t *p = fn->params; p; p = p->next) {
    ast_var_def_t *vd = (ast_var_def_t *)p;
    compile_type_expr(c, vd->type_name);
    argc++;
  }
  /* 3. is_variadic=false（M1 无用户变参函数） */
  bcode_write_op(c->bc, BCODE_PUSH_BOOL);
  bcode_write_bool(c->bc, false);
  st_push(c, 1);

  bcode_write_op(c->bc, BCODE_CREATE_FUNC_TYPE);
  bcode_write_u32(c->bc, (uint32_t)argc);
  st_push(c, -(int)(argc + 1)); /* 弹 argc+2（ret+params+variadic）压 1（sig） */

  bcode_write_op(c->bc, BCODE_PUSH_FUNCTION);
  bcode_write_u32(c->bc, (uint32_t)body);
  st_push(c, 0);

  bcode_write_op(c->bc, BCODE_DEFINE_FUNCTION);
  bcode_write_str(c->bc, fn->name);
  st_push(c, -1);
}

bytecode_t *compiler_compile(compiler_t *c, ast_node_t *program) {
  if (!c || !program || program->kind != AST_PROGRAM) {
    diag_error(c ? c->diag : NULL, (location_t){0}, "compiler: expected program");
    return NULL;
  }
  ast_program_t *prog = (ast_program_t *)program;
  c->current_scope = c->global_scope;
  c->failed = false;
  c->scope_depth = 0;
  c->stack_depth = 0;

  bytecode_t *bc = bcode_new(c->alloc);
  if (!bc) {
    diag_error(c->diag, c_loc(c, program), "compiler: out of memory creating bytecode");
    return NULL;
  }
  c->bc = bc;

  /* 函数体先编译（JMP 守卫跳过），记录各函数 entry_pc */
  size_t jmp_pc = bcode_tell(bc);
  bcode_write_op(bc, BCODE_JMP);
  bcode_write_u32(bc, 0); /* 占位，注册段起始回填 */

  size_t nfuncs = 0;
  size_t bodies[128];
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    if (f->kind != AST_FUNC_DEF) continue;
    ast_func_def_t *fn = (ast_func_def_t *)f;
    if (nfuncs < sizeof(bodies) / sizeof(bodies[0])) {
      bodies[nfuncs] = compile_func_body(c, fn);
      nfuncs++;
    }
    if (c->failed) break;
  }
  if (c->failed) {
    bcode_destroy(&bc);
    c->bc = NULL;
    return NULL;
  }

  /* 注册段 */
  size_t end = bcode_tell(bc);
  size_t fi = 0;
  for (ast_node_t *f = prog->funcs; f; f = f->next) {
    if (f->kind != AST_FUNC_DEF) continue;
    ast_func_def_t *fn = (ast_func_def_t *)f;
    compile_func_reg(c, fn, bodies[fi]);
    fi++;
    if (c->failed) break;
  }
  if (c->failed) {
    bcode_destroy(&bc);
    c->bc = NULL;
    return NULL;
  }

  bcode_write_op(bc, BCODE_HALT);
  bcode_patch_u32(bc, jmp_pc + 4, (uint32_t)end);

  c->bc = NULL; /* 产物移交调用方 */
  return bc;
}

/* ===========================================================================
 * 生命周期
 * =========================================================================== */

compiler_t *compiler_new(allocator_t *alloc, vm_t *vm, diag_buf_t *diag,
                         vec_t *tokens, sema_scope_t *global_scope) {
  if (!alloc || !vm || !diag) return NULL;
  compiler_t *c = (compiler_t *)allocator_new(alloc, &g_compiler_class, 1);
  if (!c) panic("compiler: out of memory allocating compiler");
  memset(c, 0, sizeof(compiler_t));
  c->alloc         = alloc;
  c->vm            = vm;
  c->diag          = diag;
  c->tokens        = tokens;
  c->global_scope  = global_scope;
  c->current_scope = global_scope;
  c->loop_stack    = NULL;
  c->failed        = false;
  return c;
}

void compiler_destroy(compiler_t **pc) {
  if (!pc || !*pc) return;
  compiler_t *c = *pc;
  /* 释放残留 patch 节点（编译中途失败时可能有未回填标签） */
  while (c->loop_stack) c->loop_stack = c->loop_stack->next; /* 仅断链 */
  allocator_free(c->alloc, (void **)pc);
}
