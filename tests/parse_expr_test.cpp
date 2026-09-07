/*
 * Description: parse_expr literal/primary/unary unit tests
 * Create: 2026-09-07
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "core/allocator.h"
#include "core/arena.h"
#include "core/vec.h"
#include "core/stream.h"
#include "core/panic.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/parse_expr.h"
#include "parser/parse_utils.h"
#include "parser/ast_node.h"
#include "parser/ast_kind.h"
#include "parser/ast_int_lit.h"
#include "parser/ast_float_lit.h"
#include "parser/ast_bool_lit.h"
#include "parser/ast_string_lit.h"
#include "parser/ast_char_lit.h"
#include "parser/ast_ident.h"
#include "parser/ast_unary.h"
#include "parser/ast_error.h"
}

#include "test_common.h"

namespace {

/* ---- Helper: build a token pool from source text ---- */

struct LexResult {
    vec_t       *tokens;
    lexer_t     *lexer;
    char        *source_buf;  /* heap-allocated copy, safe after move */
    size_t       source_len;
};

/**
 * Tokenize a source string into a token pool.
 * The source text is heap-allocated so its pointer remains stable
 * across LexResult moves (avoiding SSO issues with std::string).
 */
static LexResult lex_source(allocator_t *alloc, const char *src) {
    LexResult result;
    result.source_len = strlen(src);
    result.source_buf = (char *)malloc(result.source_len + 1);
    memcpy(result.source_buf, src, result.source_len + 1);

    stream_source_t mem_src = stream_source_mem(
        alloc, result.source_buf, result.source_len, false);
    istream_t *stream = istream_open(alloc, mem_src);

    lexer_t *lexer = lexer_create(alloc, stream, "test.clx");

    vec_t *pool = vec_new(alloc, true);  /* owns tokens */
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
    /* lexer_close closes the underlying istream too */
    lexer_close(&lr.lexer);
    vec_free(alloc, &lr.tokens);
    free(lr.source_buf);
    lr.source_buf = nullptr;
}

/* ---- Fixture ---- */

class ParseExprTest : public ::testing::Test {
protected:
    void SetUp() override {
        alloc_ = create_allocator(malloc, free);
        arena_ = arena_new_default(alloc_);
    }

    void TearDown() override {
        arena_destroy(alloc_, &arena_);
        EXPECT_ALLOCATOR_EMPTY_DELETE(&alloc_);
    }

    /**
     * Convenience: create a parser from source text.
     * The LexResult is stored internally and cleaned up in TearDown
     * unless the caller takes ownership.
     */
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

/* ================================================================ */
/* numeric_is_float                                                 */
/* ================================================================ */

/**
 * Scenario: Decimal number with dot is float
 * Expected: returns true
 */
TEST(NumericIsFloat, DecimalWithDot) {
    EXPECT_TRUE(numeric_is_float("3.14", 4));
}

/**
 * Scenario: Scientific notation with 'e' is float
 * Expected: returns true
 */
TEST(NumericIsFloat, ScientificNotationLowerE) {
    EXPECT_TRUE(numeric_is_float("1e10", 4));
}

/**
 * Scenario: Scientific notation with 'E' is float
 * Expected: returns true
 */
TEST(NumericIsFloat, ScientificNotationUpperE) {
    EXPECT_TRUE(numeric_is_float("1E5", 3));
}

/**
 * Scenario: Hex prefix (0x) is never float
 * Expected: returns false
 */
TEST(NumericIsFloat, HexPrefix) {
    EXPECT_FALSE(numeric_is_float("0xFF", 4));
}

/**
 * Scenario: Octal prefix (0o) is never float
 * Expected: returns false
 */
TEST(NumericIsFloat, OctalPrefix) {
    EXPECT_FALSE(numeric_is_float("0o77", 4));
}

/**
 * Scenario: Binary prefix (0b) is never float
 * Expected: returns false
 */
TEST(NumericIsFloat, BinaryPrefix) {
    EXPECT_FALSE(numeric_is_float("0b1010", 6));
}

/**
 * Scenario: Plain integer without dot or exponent
 * Expected: returns false
 */
TEST(NumericIsFloat, PlainInteger) {
    EXPECT_FALSE(numeric_is_float("42", 2));
}

/**
 * Scenario: Non-digit letters in text are not float indicators
 * Expected: returns false (no . or e/E found)
 */
TEST(NumericIsFloat, LetterINotFloat) {
    EXPECT_FALSE(numeric_is_float("3i", 2));
}

/**
 * Scenario: Non-digit letters in text are not float indicators
 * Expected: returns false
 */
TEST(NumericIsFloat, LetterUNotFloat) {
    EXPECT_FALSE(numeric_is_float("42u", 3));
}

/**
 * Scenario: Non-digit letters in text are not float indicators
 * Expected: returns false
 */
TEST(NumericIsFloat, LetterFNotFloat) {
    EXPECT_FALSE(numeric_is_float("1f", 2));
}

/**
 * Scenario: NULL text pointer returns false
 * Expected: returns false
 */
TEST(NumericIsFloat, NullTextReturnsFalse) {
    EXPECT_FALSE(numeric_is_float(NULL, 4));
}

/**
 * Scenario: Zero length returns false
 * Expected: returns false
 */
TEST(NumericIsFloat, ZeroLenReturnsFalse) {
    const char *text = "3.14";
    EXPECT_FALSE(numeric_is_float(text, 0));
}

/**
 * Scenario: Float with dot is float regardless of trailing characters
 * Expected: returns true
 */
TEST(NumericIsFloat, FloatWithTrailingChars) {
    /* dot found before any letters → still float */
    EXPECT_TRUE(numeric_is_float("3.14f", 5));
}

/**
 * Scenario: Hex with uppercase X prefix
 * Expected: returns false
 */
TEST(NumericIsFloat, HexPrefixUpperX) {
    EXPECT_FALSE(numeric_is_float("0XFF", 4));
}

/**
 * Scenario: Dot after non-digit letter is still found
 * Expected: returns true (dot found)
 */
TEST(NumericIsFloat, DotAfterLetterStillFloat) {
    /* "1i.2" — lexer won't produce this, but function still sees the dot */
    EXPECT_TRUE(numeric_is_float("1i.2", 4));
}

/**
 * Scenario: Leading zero without base prefix falls through to float check
 * Expected: returns false (plain decimal like "01")
 */
TEST(NumericIsFloat, LeadingZeroNoBasePrefix) {
    EXPECT_FALSE(numeric_is_float("01", 2));
}

/* ================================================================ */
/* parse_int_lit                                                    */
/* ================================================================ */

/**
 * Scenario: Parse decimal integer literal
 * Expected: returns AST_INT_LIT with parsed value 42, no suffix
 */
TEST_F(ParseExprTest, ParseIntLit_Decimal) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_int_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 42ULL);
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Parse hexadecimal integer literal
 * Expected: returns AST_INT_LIT with value 255 (0xFF), no suffix
 */
TEST_F(ParseExprTest, ParseIntLit_Hex) {
    parser_t *p = make_parser("0xFF");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_int_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 0xFFULL);
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Parse octal integer literal
 * Expected: returns AST_INT_LIT with value 63 (0o77), no suffix
 */
TEST_F(ParseExprTest, ParseIntLit_Octal) {
    parser_t *p = make_parser("0o77");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_int_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 63ULL);  /* 077 octal = 63 decimal */
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Parse binary integer literal
 * Expected: returns AST_INT_LIT with value 10 (0b1010), no suffix
 */
TEST_F(ParseExprTest, ParseIntLit_Binary) {
    parser_t *p = make_parser("0b1010");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_int_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 10ULL);  /* 0b1010 = 10 decimal */
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Parse integer with type suffix
 * Expected: returns AST_INT_LIT with value 42 and type "u64"
 */
TEST_F(ParseExprTest, ParseIntLit_WithSuffix) {
    /* 数字和后缀之间无空格：lexer 产出 NUMERIC "42" + IDENTIFIER "u64" */
    parser_t *p = make_parser("42u64");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_int_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 42ULL);
    EXPECT_EQ(lit->type.len, 3u);
    EXPECT_EQ(memcmp(lit->type.ptr, "u64", 3), 0);

    cleanup_parser(p);
}

/**
 * Scenario: Integer with invalid suffix (not a type suffix) is ignored
 * Expected: returns AST_INT_LIT with value 42, type empty
 */
TEST_F(ParseExprTest, ParseIntLit_InvalidSuffixIgnored) {
    parser_t *p = make_parser("42 hello");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_int_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 42ULL);
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Float token does not match int lit
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseIntLit_FloatReturnsNull) {
    parser_t *p = make_parser("3.14");
    ASSERT_NE(p, nullptr);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_int_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/**
 * Scenario: Non-numeric token does not match int lit
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseIntLit_NonNumericReturnsNull) {
    parser_t *p = make_parser("hello");
    ASSERT_NE(p, nullptr);

    /* skip whitespace to land on the identifier token */
    skip_trivia(p);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_int_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_float_lit                                                  */
/* ================================================================ */

/**
 * Scenario: Parse decimal float literal
 * Expected: returns AST_FLOAT_LIT with value 3.14, no suffix
 */
TEST_F(ParseExprTest, ParseFloatLit_DecimalDot) {
    parser_t *p = make_parser("3.14");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_float_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_FLOAT_LIT);

    auto *lit = (ast_float_lit_t *)node;
    EXPECT_DOUBLE_EQ(lit->value, 3.14);
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Parse float in scientific notation
 * Expected: returns AST_FLOAT_LIT with value 1e10, no suffix
 */
TEST_F(ParseExprTest, ParseFloatLit_Scientific) {
    parser_t *p = make_parser("1e10");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_float_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_FLOAT_LIT);

    auto *lit = (ast_float_lit_t *)node;
    EXPECT_DOUBLE_EQ(lit->value, 1e10);
    EXPECT_TRUE(strslice_is_empty(lit->type));

    cleanup_parser(p);
}

/**
 * Scenario: Parse float with type suffix
 * Expected: returns AST_FLOAT_LIT with value 3.14 and type "f32"
 */
TEST_F(ParseExprTest, ParseFloatLit_WithSuffix) {
    /* 3.14f32 → NUMERIC "3.14" + KEYWORD "f32" */
    parser_t *p = make_parser("3.14f32");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_float_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_FLOAT_LIT);

    auto *lit = (ast_float_lit_t *)node;
    EXPECT_DOUBLE_EQ(lit->value, 3.14);
    EXPECT_EQ(lit->type.len, 3u);
    EXPECT_EQ(memcmp(lit->type.ptr, "f32", 3), 0);

    cleanup_parser(p);
}

/**
 * Scenario: Integer-number with float suffix is a float literal
 * Expected: 1f32 → value 1.0, type "f32"
 */
TEST_F(ParseExprTest, ParseFloatLit_IntNumWithFloatSuffix) {
    parser_t *p = make_parser("1f32");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_float_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_FLOAT_LIT);

    auto *lit = (ast_float_lit_t *)node;
    EXPECT_DOUBLE_EQ(lit->value, 1.0);
    EXPECT_EQ(lit->type.len, 3u);
    EXPECT_EQ(memcmp(lit->type.ptr, "f32", 3), 0);

    cleanup_parser(p);
}

/**
 * Scenario: Integer token does not match float lit
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseFloatLit_IntegerReturnsNull) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_float_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/**
 * Scenario: Non-numeric token does not match float lit
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseFloatLit_NonNumericReturnsNull) {
    parser_t *p = make_parser("hello");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_float_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_bool_lit                                                   */
/* ================================================================ */

/**
 * Scenario: Parse "true" keyword as bool literal
 * Expected: returns AST_BOOL_LIT with value == true
 */
TEST_F(ParseExprTest, ParseBoolLit_True) {
    parser_t *p = make_parser("true");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_bool_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_BOOL_LIT);

    auto *lit = (ast_bool_lit_t *)node;
    EXPECT_TRUE(lit->value);

    cleanup_parser(p);
}

/**
 * Scenario: Parse "false" keyword as bool literal
 * Expected: returns AST_BOOL_LIT with value == false
 */
TEST_F(ParseExprTest, ParseBoolLit_False) {
    parser_t *p = make_parser("false");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_bool_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_BOOL_LIT);

    auto *lit = (ast_bool_lit_t *)node;
    EXPECT_FALSE(lit->value);

    cleanup_parser(p);
}

/**
 * Scenario: Identifier token is not a bool keyword
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseBoolLit_NonKeywordReturnsNull) {
    parser_t *p = make_parser("hello");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_bool_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/**
 * Scenario: Integer token is not a bool keyword
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseBoolLit_NumericReturnsNull) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_bool_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_string_lit                                                 */
/* ================================================================ */

/**
 * Scenario: Parse string literal token
 * Expected: returns AST_STRING_LIT with resolved text (no quotes, no escapes)
 */
TEST_F(ParseExprTest, ParseStringLit_Basic) {
    parser_t *p = make_parser("\"hello\"");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_string_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_STRING_LIT);

    auto *lit = (ast_string_lit_t *)node;
    /* text is escape-resolved, quotes stripped */
    EXPECT_EQ(lit->text.len, 5u);
    EXPECT_EQ(memcmp(lit->text.ptr, "hello", 5), 0);

    cleanup_parser(p);
}

/**
 * Scenario: Non-string token does not match string lit
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseStringLit_NonStringReturnsNull) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_string_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_char_lit                                                   */
/* ================================================================ */

/**
 * Scenario: Parse character literal token
 * Expected: returns AST_CHAR_LIT with value 'a' (97)
 */
TEST_F(ParseExprTest, ParseCharLit_Basic) {
    parser_t *p = make_parser("'a'");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_char_lit(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_CHAR_LIT);

    auto *lit = (ast_char_lit_t *)node;
    EXPECT_EQ(lit->value, (uint32_t)'a');

    cleanup_parser(p);
}

/**
 * Scenario: Non-character token does not match char lit
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseCharLit_NonCharReturnsNull) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_char_lit(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_ident                                                      */
/* ================================================================ */

/**
 * Scenario: Parse identifier token
 * Expected: returns AST_IDENT with correct name
 */
TEST_F(ParseExprTest, ParseIdent_Basic) {
    parser_t *p = make_parser("foo");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_ident(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_IDENT);

    auto *ident = (ast_ident_t *)node;
    EXPECT_EQ(ident->name.len, 3u);
    EXPECT_EQ(memcmp(ident->name.ptr, "foo", 3), 0);

    cleanup_parser(p);
}

/**
 * Scenario: Non-identifier token does not match ident
 * Expected: returns NULL and restores cursor
 */
TEST_F(ParseExprTest, ParseIdent_NonIdentReturnsNull) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    uint32_t pos_before = p->pos;
    ast_node_t *node = parse_ident(p);
    EXPECT_EQ(node, nullptr);
    EXPECT_EQ(p->pos, pos_before);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_primary                                                    */
/* ================================================================ */

/**
 * Scenario: Grouped expression with parentheses
 * Expected: returns the inner expression (int lit 42)
 */
TEST_F(ParseExprTest, ParsePrimary_GroupedExpr) {
    parser_t *p = make_parser("(42)");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_primary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 42ULL);

    cleanup_parser(p);
}

/**
 * Scenario: Grouped expression missing closing paren
 * Expected: returns AST_ERROR
 */
TEST_F(ParseExprTest, ParsePrimary_GroupedMissingCloseParen) {
    parser_t *p = make_parser("(42");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_primary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: Empty parentheses yield AST_ERROR (inner expr is NULL)
 * Expected: returns AST_ERROR
 */
TEST_F(ParseExprTest, ParsePrimary_EmptyParensReturnsError) {
    parser_t *p = make_parser("()");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_primary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: No matching primary expression (bare symbol)
 * Expected: returns NULL
 */
TEST_F(ParseExprTest, ParsePrimary_NoMatchReturnsNull) {
    /* A bare '+' symbol does not start any primary */
    parser_t *p = make_parser("+");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_primary(p);
    EXPECT_EQ(node, nullptr);

    cleanup_parser(p);
}

/**
 * Scenario: String literal matches in primary
 * Expected: returns AST_STRING_LIT
 */
TEST_F(ParseExprTest, ParsePrimary_StringLit) {
    parser_t *p = make_parser("\"hello\"");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_primary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_STRING_LIT);

    auto *lit = (ast_string_lit_t *)node;
    EXPECT_EQ(lit->text.len, 5u);
    EXPECT_EQ(memcmp(lit->text.ptr, "hello", 5), 0);

    cleanup_parser(p);
}

/**
 * Scenario: Bool literal true matches in primary (before ident check)
 * Expected: returns AST_BOOL_LIT, not AST_IDENT
 */
TEST_F(ParseExprTest, ParsePrimary_BoolTrueBeforeIdent) {
    parser_t *p = make_parser("true");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_primary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_BOOL_LIT);

    auto *lit = (ast_bool_lit_t *)node;
    EXPECT_TRUE(lit->value);

    cleanup_parser(p);
}

/* ================================================================ */
/* parse_unary                                                      */
/* ================================================================ */

/**
 * Scenario: Logical NOT prefix operator
 * Expected: returns AST_UNARY with op='!' and identifier operand
 */
TEST_F(ParseExprTest, ParseUnary_Bang) {
    parser_t *p = make_parser("!x");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_UNARY);

    auto *unary = (ast_unary_t *)node;
    EXPECT_EQ(unary->op, '!');
    ASSERT_NE(unary->operand, nullptr);
    EXPECT_EQ(unary->operand->kind, AST_IDENT);

    cleanup_parser(p);
}

/**
 * Scenario: Bitwise NOT prefix operator
 * Expected: returns AST_UNARY with op='~'
 */
TEST_F(ParseExprTest, ParseUnary_Tilde) {
    parser_t *p = make_parser("~x");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_UNARY);

    auto *unary = (ast_unary_t *)node;
    EXPECT_EQ(unary->op, '~');
    ASSERT_NE(unary->operand, nullptr);
    EXPECT_EQ(unary->operand->kind, AST_IDENT);

    cleanup_parser(p);
}

/**
 * Scenario: Negation prefix operator
 * Expected: returns AST_UNARY with op='-'
 */
TEST_F(ParseExprTest, ParseUnary_Minus) {
    parser_t *p = make_parser("-x");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_UNARY);

    auto *unary = (ast_unary_t *)node;
    EXPECT_EQ(unary->op, '-');
    ASSERT_NE(unary->operand, nullptr);
    EXPECT_EQ(unary->operand->kind, AST_IDENT);

    cleanup_parser(p);
}

/**
 * Scenario: Double logical NOT (recursive unary)
 * Expected: returns nested AST_UNARY, outer op='!' inner op='!'
 */
TEST_F(ParseExprTest, ParseUnary_DoubleBang) {
    parser_t *p = make_parser("!!x");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_UNARY);

    auto *outer = (ast_unary_t *)node;
    EXPECT_EQ(outer->op, '!');

    ASSERT_NE(outer->operand, nullptr);
    EXPECT_EQ(outer->operand->kind, AST_UNARY);

    auto *inner = (ast_unary_t *)outer->operand;
    EXPECT_EQ(inner->op, '!');
    ASSERT_NE(inner->operand, nullptr);
    EXPECT_EQ(inner->operand->kind, AST_IDENT);

    cleanup_parser(p);
}

/**
 * Scenario: No prefix operator falls through to parse_primary
 * Expected: returns AST_INT_LIT (primary matched)
 */
TEST_F(ParseExprTest, ParseUnary_NoPrefixFallsToPrimary) {
    parser_t *p = make_parser("42");
    ASSERT_NE(p, nullptr);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_INT_LIT);

    auto *lit = (ast_int_lit_t *)node;
    EXPECT_EQ(lit->value, 42ULL);

    cleanup_parser(p);
}

/**
 * Scenario: Prefix operator with no operand (at EOF) returns AST_ERROR
 * Expected: returns AST_ERROR node
 */
TEST_F(ParseExprTest, ParseUnary_MissingOperandReturnsError) {
    parser_t *p = make_parser("!");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: Double prefix where inner has no operand propagates AST_ERROR
 * Expected: returns AST_ERROR (not NULL) — error propagation path
 */
TEST_F(ParseExprTest, ParseUnary_DoubleBangMissingOperand) {
    parser_t *p = make_parser("!!");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_ERROR);

    cleanup_parser(p);
}

/**
 * Scenario: Negation of integer literal
 * Expected: returns AST_UNARY with op='-', operand is AST_INT_LIT
 */
TEST_F(ParseExprTest, ParseUnary_MinusIntLit) {
    parser_t *p = make_parser("-42");
    ASSERT_NE(p, nullptr);

    skip_trivia(p);

    ast_node_t *node = parse_unary(p);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->kind, AST_UNARY);

    auto *unary = (ast_unary_t *)node;
    EXPECT_EQ(unary->op, '-');
    ASSERT_NE(unary->operand, nullptr);
    EXPECT_EQ(unary->operand->kind, AST_INT_LIT);

    cleanup_parser(p);
}

} // namespace
