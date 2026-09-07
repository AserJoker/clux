/*
 * Description: parse_assign_or_expr_stmt unit tests
 * Create: 2026-09-07
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "core/allocator.h"
#include "core/arena.h"
#include "core/vec.h"
#include "core/stream.h"
#include "core/panic.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/parse_stmt.h"
#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_node.h"
#include "parser/ast_kind.h"
#include "parser/ast_ident.h"
#include "parser/ast_assign.h"
#include "parser/ast_expr_stmt.h"
#include "parser/ast_discard.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_call.h"
#include "parser/ast_binary.h"
#include "parser/ast_error.h"
}

#include "test_common.h"

namespace {

/* ---- Helper: build a token pool from source text ---- */

struct LexResult {
    vec_t       *tokens;
    lexer_t     *lexer;
    char        *source_buf;
    size_t       source_len;
};

static LexResult lex_source(allocator_t *alloc, const char *src) {
    LexResult result;
    result.source_len = strlen(src);
    result.source_buf = (char *)malloc(result.source_len + 1);
    memcpy(result.source_buf, src, result.source_len + 1);

    stream_source_t mem_src = stream_source_mem(
        alloc, result.source_buf, result.source_len, false);
    istream_t *stream = istream_open(alloc, mem_src);

    lexer_t *lexer = lexer_create(alloc, stream, "test.clx");

    vec_t *pool = vec_new(alloc, true);
    for (;;) {
        token_t *t = lexer_next(lexer);
        token_kind_t k = token_get_kind(t);
        vec_push(pool, alloc, t);
        if (k == TOKEN_TYPE_EOF || k == TOKEN_TYPE_ERROR) break;
    }

    result.tokens = pool;
    result.lexer = lexer;
    return result;
}

static void lex_result_destroy(allocator_t *alloc, LexResult &lr) {
    lexer_close(&lr.lexer);
    vec_free(alloc, &lr.tokens);
    free(lr.source_buf);
    lr.source_buf = nullptr;
}

/* ---- Fixture ---- */

class ParseStmtTest : public ::testing::Test {
protected:
    void SetUp() override {
        alloc_ = create_allocator(malloc, free);
        arena_ = arena_new_default(alloc_);
    }

    void TearDown() override {
        arena_destroy(alloc_, &arena_);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc_);
    }

    parser_t *make_parser(const char *src) {
        last_lex_ = lex_source(alloc_, src);
        return parser_create(alloc_, arena_, last_lex_.tokens);
    }

    void cleanup_parser(parser_t *p) {
        parser_destroy(&p);
        lex_result_destroy(alloc_, last_lex_);
    }

    allocator_t *alloc_ = nullptr;
    arena_t     *arena_ = nullptr;
    LexResult    last_lex_;
};

/* ---- 辅助：断言 token 文本 ---- */

static void expect_token_text(const token_t *tok, const char *expected) {
    ASSERT_NE(tok, nullptr);
    strslice_t s = token_strslice(tok);
    EXPECT_EQ(s.len, strlen(expected));
    EXPECT_EQ(memcmp(s.ptr, expected, s.len), 0);
}

/* ================================================================ */
/* Expression Statement: expr;                                       */
/* ================================================================ */

/**
 * Scenario: Simple function call as expression statement
 * Expected: AST_EXPR_STMT with expr being AST_CALL
 */
TEST_F(ParseStmtTest, ExprStmt_FunctionCall) {
    parser_t *p = make_parser("foo();");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_EXPR_STMT);

    auto *stmt = (ast_expr_stmt_t *)node;
    ASSERT_NE(stmt->expr, nullptr);
    EXPECT_EQ(stmt->expr->kind, AST_CALL);

    cleanup_parser(p);
}

/**
 * Scenario: Integer literal as expression statement
 * Expected: AST_EXPR_STMT with expr being AST_INT_LIT
 */
TEST_F(ParseStmtTest, ExprStmt_IntLiteral) {
    parser_t *p = make_parser("42;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_EXPR_STMT);

    auto *stmt = (ast_expr_stmt_t *)node;
    ASSERT_NE(stmt->expr, nullptr);
    EXPECT_EQ(stmt->expr->kind, AST_INT_LIT);

    cleanup_parser(p);
}

/**
 * Scenario: Binary expression as expression statement
 * Expected: AST_EXPR_STMT with expr being AST_BINARY
 */
TEST_F(ParseStmtTest, ExprStmt_BinaryExpr) {
    parser_t *p = make_parser("a + b;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_EXPR_STMT);

    auto *stmt = (ast_expr_stmt_t *)node;
    ASSERT_NE(stmt->expr, nullptr);
    EXPECT_EQ(stmt->expr->kind, AST_BINARY);

    cleanup_parser(p);
}

/**
 * Scenario: Missing semicolon after expression
 * Expected: AST_ERROR
 */
TEST_F(ParseStmtTest, ExprStmt_MissingSemicolon) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/* ================================================================ */
/* Assignment: name = expr; / name += expr; etc.                     */
/* ================================================================ */

/**
 * Scenario: Simple assignment x = 10;
 * Expected: AST_ASSIGN with name="x", op="=", value is AST_INT_LIT
 */
TEST_F(ParseStmtTest, Assign_Simple) {
    parser_t *p = make_parser("x = 10;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    EXPECT_EQ(assign->name.len, 1u);
    EXPECT_EQ(assign->name.ptr[0], 'x');
    expect_token_text(assign->op, "=");
    ASSERT_NE(assign->value, nullptr);
    EXPECT_EQ(assign->value->kind, AST_INT_LIT);

    cleanup_parser(p);
}

/**
 * Scenario: Compound assignment y += 5;
 * Expected: AST_ASSIGN with op="+="
 */
TEST_F(ParseStmtTest, Assign_CompoundAdd) {
    parser_t *p = make_parser("y += 5;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    expect_token_text(assign->op, "+=");
    ASSERT_NE(assign->value, nullptr);
    EXPECT_EQ(assign->value->kind, AST_INT_LIT);

    cleanup_parser(p);
}

/**
 * Scenario: Compound subtraction assignment z -= 3;
 * Expected: AST_ASSIGN with op="-="
 */
TEST_F(ParseStmtTest, Assign_CompoundSub) {
    parser_t *p = make_parser("z -= 3;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    expect_token_text(assign->op, "-=");

    cleanup_parser(p);
}

/**
 * Scenario: Compound multiplication assignment m *= 2;
 * Expected: AST_ASSIGN with op="*="
 */
TEST_F(ParseStmtTest, Assign_CompoundMul) {
    parser_t *p = make_parser("m *= 2;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    expect_token_text(assign->op, "*=");

    cleanup_parser(p);
}

/**
 * Scenario: Compound division assignment d /= 4;
 * Expected: AST_ASSIGN with op="/="
 */
TEST_F(ParseStmtTest, Assign_CompoundDiv) {
    parser_t *p = make_parser("d /= 4;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    expect_token_text(assign->op, "/=");

    cleanup_parser(p);
}

/**
 * Scenario: Compound modulo assignment r %= 2;
 * Expected: AST_ASSIGN with op="%="
 */
TEST_F(ParseStmtTest, Assign_CompoundMod) {
    parser_t *p = make_parser("r %= 2;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    expect_token_text(assign->op, "%=");

    cleanup_parser(p);
}

/**
 * Scenario: Assignment with expression RHS: result = a + b * c;
 * Expected: AST_ASSIGN, value is AST_BINARY
 */
TEST_F(ParseStmtTest, Assign_ExprRHS) {
    parser_t *p = make_parser("result = a + b * c;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    ASSERT_NE(assign->value, nullptr);
    EXPECT_EQ(assign->value->kind, AST_BINARY);

    cleanup_parser(p);
}

/**
 * Scenario: Assignment missing semicolon: x = 10
 * Expected: AST_ERROR
 */
TEST_F(ParseStmtTest, Assign_MissingSemicolon) {
    parser_t *p = make_parser("x = 10");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: Assignment with missing RHS: x = ;
 * Expected: AST_ERROR
 */
TEST_F(ParseStmtTest, Assign_MissingRHS) {
    parser_t *p = make_parser("x = ;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/* ================================================================ */
/* Discard: _ = expr;                                                */
/* ================================================================ */

/**
 * Scenario: Discard statement _ = foo();
 * Expected: AST_DISCARD with expr being AST_CALL
 */
TEST_F(ParseStmtTest, Discard_FunctionCall) {
    parser_t *p = make_parser("_ = foo();");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_DISCARD);

    auto *discard = (ast_discard_t *)node;
    ASSERT_NE(discard->expr, nullptr);
    EXPECT_EQ(discard->expr->kind, AST_CALL);

    cleanup_parser(p);
}

/**
 * Scenario: Discard with integer literal: _ = 42;
 * Expected: AST_DISCARD with expr being AST_INT_LIT
 */
TEST_F(ParseStmtTest, Discard_IntLiteral) {
    parser_t *p = make_parser("_ = 42;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_DISCARD);

    auto *discard = (ast_discard_t *)node;
    ASSERT_NE(discard->expr, nullptr);
    EXPECT_EQ(discard->expr->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)discard->expr;
    EXPECT_EQ(lit->value, 42ULL);

    cleanup_parser(p);
}

/**
 * Scenario: Discard with binary expression: _ = a + b;
 * Expected: AST_DISCARD with expr being AST_BINARY
 */
TEST_F(ParseStmtTest, Discard_BinaryExpr) {
    parser_t *p = make_parser("_ = a + b;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_DISCARD);

    auto *discard = (ast_discard_t *)node;
    ASSERT_NE(discard->expr, nullptr);
    EXPECT_EQ(discard->expr->kind, AST_BINARY);

    cleanup_parser(p);
}

/**
 * Scenario: Discard missing semicolon: _ = 42
 * Expected: AST_ERROR
 */
TEST_F(ParseStmtTest, Discard_MissingSemicolon) {
    parser_t *p = make_parser("_ = 42");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: Discard missing RHS: _ = ;
 * Expected: AST_ERROR
 */
TEST_F(ParseStmtTest, Discard_MissingRHS) {
    parser_t *p = make_parser("_ = ;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: _ is NOT a discard when used as compound assignment
 * _ += 1; should be AST_ASSIGN (not DISCARD), because only plain = triggers discard
 * Expected: AST_ASSIGN with name="_"
 */
TEST_F(ParseStmtTest, Underscore_CompoundAssignIsNotDiscard) {
    parser_t *p = make_parser("_ += 1;");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_assign_or_expr_stmt(p);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->kind, AST_ASSIGN);

    auto *assign = (ast_assign_t *)node;
    EXPECT_EQ(assign->name.len, 1u);
    EXPECT_EQ(assign->name.ptr[0], '_');
    expect_token_text(assign->op, "+=");

    cleanup_parser(p);
}

} // namespace
