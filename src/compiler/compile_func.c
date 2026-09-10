#include "compiler/compiler.h"
#include "parser/ast_block.h"
#include "parser/ast_func_def.h"
#include "parser/ast_var_def.h"

/* ===========================================================================
 * 函数节点
 * =========================================================================== */

/**
 * 编译单个函数体：
 *  - 倒序 DEFINE 绑参（参数定义到 func_vcall 推入的匿名局部作用域）
 *  - DEFINE 完成后 PUSH_SCOPE：函数体临时变量/块内变量定义到独立子作用域，
 *    与参数隔离（同名不冲突，且临时变量随函数返回销毁）
 *  - body 语句 → POP_SCOPE → return 兜底
 * 返回函数体入口（entry_pc），供注册段 PUSH_FUNCTION 引用。
 */
size_t compile_func_body(compiler_t *c, ast_func_def_t *fn) {
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
void compile_func_reg(compiler_t *c, ast_func_def_t *fn, size_t body) {
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
