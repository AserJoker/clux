#ifndef _H_CLUX_PARSER_AST_KIND_
#define _H_CLUX_PARSER_AST_KIND_

typedef enum {
    /* --- 顶层 --- */
    AST_PROGRAM,         /* 函数定义列表 */
    AST_FUNC_DEF,        /* func name(params):type { body } */

    /* --- 语句 --- */
    AST_VAR_DEF,         /* var name[:type] [= init]; */
    AST_ASSIGN,          /* name = expr; / name += expr; */
    AST_IF,              /* if cond { then } [else { else_body }] */
    AST_WHILE,           /* while cond { body } */
    AST_FOR,             /* for (init; cond; update) { body } */
    AST_RETURN,          /* return [expr]; */
    AST_BREAK,           /* break; */
    AST_CONTINUE,        /* continue; */
    AST_BLOCK,           /* { stmts... } */
    AST_EXPR_STMT,       /* expr;（表达式作为语句） */
    /* AST_DISCARD removed: _ = expr is AST_ASSIGN, discard semantics in Sema */

    /* --- 表达式 --- */
    AST_BINARY,          /* lhs op rhs */
    AST_UNARY,           /* op expr */
    AST_CALL,            /* callee(args...) */
    AST_MEMBER,          /* expr.field */
    AST_INDEX,           /* expr[expr, ...]（下标 / 泛型实例化，语义阶段区分） */
    AST_INT_LIT,         /* 整数字面量 */
    AST_FLOAT_LIT,       /* 浮点字面量 */
    AST_BOOL_LIT,        /* true / false */
    AST_STRING_LIT,      /* "..."（escape 展开后文本） */
    AST_CHAR_LIT,        /* 'a'（u8 码点值） */
    AST_IDENT,           /* 标识符引用 */
    AST_CAST,            /* expr as type */

    AST_ERROR,           /* 解析错误恢复节点（记录错误位置，占位） */

    AST_KIND_COUNT,      /* 哨兵值，用于数组索引 */
} ast_kind_t;

#endif /* _H_CLUX_PARSER_AST_KIND_ */
