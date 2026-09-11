#include "compiler/compiler.h"
#include "parser/type_qual.h"

/* ===========================================================================
 * 类型表达式（M1：identifier 一种形式 → LOAD "name" + 可选限定构造）
 * =========================================================================== */

void compile_type_expr(compiler_t *c, strslice_t type_name, type_qual_t qual) {
  if (strslice_is_empty(type_name)) {
    /* 空类型：PUSH_UNDEFINED（"待推导"占位，op_define 从值推断）。
       限定符无意义（无基础类型可修饰），sema 已保证限定符不带空类型。 */
    bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED);
    st_push(c, 1);
    return;
  }
  /* 类型当作表达式：LOAD 从 global scope 查 type value 压栈 */
  bcode_write_op(c->bc, BCODE_LOAD);
  bcode_write_str(c->bc, type_name);
  st_push(c, 1);
  /* 限定位应用（固定组合顺序 volatile(const(T))：先 const 后 volatile，
     与 resolve_type_q 一致） */
  if (qual & TYPE_QUAL_CONST) {
    bcode_write_op(c->bc, BCODE_CREATE_CONST);
    st_push(c, 0); /* 弹一压一，栈深不变 */
  }
  if (qual & TYPE_QUAL_VOLATILE) {
    bcode_write_op(c->bc, BCODE_CREATE_VOLATILE);
    st_push(c, 0);
  }
}
