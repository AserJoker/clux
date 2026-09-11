#include "compiler/compiler.h"
#include "parser/ast_const.h"
#include "parser/ast_ident.h"
#include "parser/ast_volatile.h"

/* ===========================================================================
 * 类型表达式编译（类型即表达式，m2-design 关键架构决策 6）
 *
 * 类型槽位 = 普通表达式节点。命名类型（AST_IDENT）→ BCODE_PUSH 从当前
 * 作用域链查 type value 压栈（内建类型注册在 global scope，自定义类型注册
 * 到定义点当前作用域，遮蔽语义与变量同机制）；AST_CONST/AST_VOLATILE →
 * 递归编译 sub 后应用 BCODE_CREATE_CONST/VOLATILE。
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

  if (type_expr->kind == AST_IDENT) {
    /* 类型名当作表达式：PUSH 从当前作用域链查 type value 压栈
       （内建类型在 global scope，自定义类型在当前/root scope，天然遮罩） */
    ast_ident_t *id = (ast_ident_t *)type_expr;
    bcode_write_op(c->bc, BCODE_PUSH);
    bcode_write_str(c->bc, id->name);
    st_push(c, 1);
    return;
  }

  if (type_expr->kind == AST_CONST) {
    /* const 修饰：递归编译 sub（类型表达式），再应用 const 构造。
       固定组合顺序 volatile(const(T))：先 const 后 volatile，与
       resolve_type_expr 一致。弹一压一，栈深不变。 */
    compile_type_expr(c, ((ast_const_t *)type_expr)->sub);
    bcode_write_op(c->bc, BCODE_CREATE_CONST);
    return;
  }

  if (type_expr->kind == AST_VOLATILE) {
    compile_type_expr(c, ((ast_volatile_t *)type_expr)->sub);
    bcode_write_op(c->bc, BCODE_CREATE_VOLATILE);
    return;
  }

  /* M2 扩展点：数组/元组/func 类型表达式（sema 已保证可达此处时合法） */
  bcode_write_op(c->bc, BCODE_PUSH_UNDEFINED);
  st_push(c, 1);
}
