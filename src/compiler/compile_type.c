#include "compiler/compiler.h"

/* ===========================================================================
 * 类型表达式（M1：仅 identifier 一种形式 → LOAD "name"）
 * =========================================================================== */

void compile_type_expr(compiler_t *c, strslice_t type_name) {
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
