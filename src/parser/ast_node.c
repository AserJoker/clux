#include "parser/ast_node.h"

/* ---- Kind name / size tables ---- */

#include "parser/ast_program.h"
#include "parser/ast_func_def.h"
#include "parser/ast_var_def.h"
#include "parser/ast_assign.h"
#include "parser/ast_if.h"
#include "parser/ast_while.h"
#include "parser/ast_for.h"
#include "parser/ast_return.h"
#include "parser/ast_block.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_discard.h"
#include "parser/ast_binary.h"
#include "parser/ast_unary.h"
#include "parser/ast_call.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_ident.h"
#include "parser/ast_cast.h"
#include "parser/ast_error.h"

static const char *g_kind_names[] = {
    [AST_PROGRAM]    = "program",
    [AST_FUNC_DEF]   = "func_def",
    [AST_VAR_DEF]    = "var_def",
    [AST_ASSIGN]     = "assign",
    [AST_IF]         = "if",
    [AST_WHILE]      = "while",
    [AST_FOR]        = "for",
    [AST_RETURN]     = "return",
    [AST_BREAK]      = "break",
    [AST_CONTINUE]   = "continue",
    [AST_BLOCK]      = "block",
    [AST_EXPR_STMT]  = "expr_stmt",
    [AST_DISCARD]    = "discard",
    [AST_BINARY]     = "binary",
    [AST_UNARY]      = "unary",
    [AST_CALL]       = "call",
    [AST_INT_LIT]    = "int_lit",
    [AST_FLOAT_LIT]  = "float_lit",
    [AST_BOOL_LIT]   = "bool_lit",
    [AST_STRING_LIT] = "string_lit",
    [AST_CHAR_LIT]   = "char_lit",
    [AST_IDENT]      = "ident",
    [AST_CAST]       = "cast",
    [AST_ERROR]      = "error",
};

static const size_t g_kind_sizes[] = {
    [AST_PROGRAM]    = sizeof(ast_program_t),
    [AST_FUNC_DEF]   = sizeof(ast_func_def_t),
    [AST_VAR_DEF]    = sizeof(ast_var_def_t),
    [AST_ASSIGN]     = sizeof(ast_assign_t),
    [AST_IF]         = sizeof(ast_if_t),
    [AST_WHILE]      = sizeof(ast_while_t),
    [AST_FOR]        = sizeof(ast_for_t),
    [AST_RETURN]     = sizeof(ast_return_t),
    [AST_BREAK]      = sizeof(ast_node_t),
    [AST_CONTINUE]   = sizeof(ast_node_t),
    [AST_BLOCK]      = sizeof(ast_block_t),
    [AST_EXPR_STMT]  = sizeof(ast_expr_stmt_t),
    [AST_DISCARD]    = sizeof(ast_discard_t),
    [AST_BINARY]     = sizeof(ast_binary_t),
    [AST_UNARY]      = sizeof(ast_unary_t),
    [AST_CALL]       = sizeof(ast_call_t),
    [AST_INT_LIT]    = sizeof(ast_int_lit_t),
    [AST_FLOAT_LIT]  = sizeof(ast_float_lit_t),
    [AST_BOOL_LIT]   = sizeof(ast_bool_lit_t),
    [AST_STRING_LIT] = sizeof(ast_string_lit_t),
    [AST_CHAR_LIT]   = sizeof(ast_char_lit_t),
    [AST_IDENT]      = sizeof(ast_ident_t),
    [AST_CAST]       = sizeof(ast_cast_t),
    [AST_ERROR]      = sizeof(ast_error_t),
};

/* ---- Public API ---- */

void ast_append(ast_node_t **head, ast_node_t **last,
                ast_node_t *parent, ast_node_t *node) {
    if (!head || !last || !node) return;

    node->parent = parent;
    node->next   = NULL;

    if (!*head) {
        *head = node;
        *last = node;
    } else {
        (*last)->next = node;
        *last = node;
    }
}

const char *ast_kind_name(ast_kind_t kind) {
    if (kind < 0 || kind >= AST_KIND_COUNT) return "unknown";
    return g_kind_names[kind];
}

size_t ast_kind_size(ast_kind_t kind) {
    if (kind < 0 || kind >= AST_KIND_COUNT) return 0;
    return g_kind_sizes[kind];
}
