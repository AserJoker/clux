#include "compiler/compiler.h"
#include "parser/ast_type_name.h"

/* ===========================================================================
 * 类型表达式编译（类型即表达式，m2-design 关键架构决策 6）
 *
 * M1：AST_TYPE_NAME 一种形式 → LOAD "name" + 可选限定构造（const/volatile）。
 * M2 扩展：数组/元组/func 类型表达式在此按类型构造协议编译。
 * 空类型（type_expr == NULL）→ PUSH_UNDEFINED（"待推导"占位，op_define 从值推断）。
 * =========================================================================== */

void compile_type_expr(compiler_t *c, ast_node_t *type_expr) {
  if (!type_expr) {
    /* 空类型：PUSH_UNDEFINED（"待推导"占位，op_define 从值推断）。
       限定符无意义（无基础类型可修饰），sema 已保证限定符不带空类型。 */
    bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED);
    st_push(c, 1);
    return;
  }

  if (type_expr->kind == AST_TYPE_NAME) {
    ast_type_name_t *tn = (ast_type_name_t *)type_expr;
    /* 类型当作表达式：LOAD 从 global scope 查 type value 压栈 */
    bcode_write_op(c->bc, BCODE_LOAD);
    bcode_write_str(c->bc, tn->name);
    st_push(c, 1);
    /* 限定位应用（固定组合顺序 volatile(const(T))：先 const 后 volatile，
       与 resolve_type_expr 一致） */
    if (tn->qual & TYPE_QUAL_CONST) {
      bcode_write_op(c->bc, BCODE_CREATE_CONST);
      st_push(c, 0); /* 弹一压一，栈深不变 */
    }
    if (tn->qual & TYPE_QUAL_VOLATILE) {
      bcode_write_op(c->bc, BCODE_CREATE_VOLATILE);
      st_push(c, 0);
    }
    return;
  }

  /* M2 扩展点：数组/元组/func 类型表达式（sema 已保证可达此处时合法） */
  bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED);
  st_push(c, 1);
}
