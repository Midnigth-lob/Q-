/*
 * Q+ Compiler — Lexer Test Suite
 *
 * Self-contained test program that verifies the lexer correctly
 * tokenizes all Q+ language constructs.
 */

#include "qpc/common.h"
#include "qpc/token.h"
#include "qpc/source.h"
#include "qpc/lexer.h"
#include "qpc/diagnostic.h"

/* ── Test infrastructure ─────────────────────────────────────── */

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        g_tests_run++; \
        diag_reset(); \
        printf("  TEST: %-40s ", #name); \
        test_##name(); \
    } \
    static void test_##name(void)

#define PASS() do { g_tests_passed++; printf("[PASS]\n"); return; } while(0)
#define FAIL(msg) do { \
    g_tests_failed++; \
    printf("[FAIL]\n    >> %s\n", msg); \
    return; \
} while(0)

#define ASSERT_EQ(a, b, msg) do { if ((a) != (b)) FAIL(msg); } while(0)
#define ASSERT_TRUE(cond, msg) do { if (!(cond)) FAIL(msg); } while(0)

/* Helper: lex a string and return tokens */
static Token *lex_str(const char *code, u32 *count) {
    static int test_src_counter = 0;
    char name[64];
    snprintf(name, sizeof(name), "<test_%d>", test_src_counter++);

    i32 src_id = source_load_string(name, code);
    if (src_id < 0) {
        *count = 0;
        return NULL;
    }

    SourceFile *src = source_get((u16)src_id);
    Lexer lex;
    lexer_init(&lex, src);
    return lexer_lex_all(&lex, count);
}

/* ── Tests ────────────────────────────────────────────────────── */

TEST(empty_input) {
    u32 count;
    Token *toks = lex_str("", &count);
    ASSERT_EQ(count, 1, "empty input should produce 1 token (EOF)");
    ASSERT_EQ(toks[0].kind, TOK_EOF, "single token should be EOF");
    free(toks);
    PASS();
}

TEST(whitespace_only) {
    u32 count;
    Token *toks = lex_str("   \t\n  \r\n  ", &count);
    ASSERT_EQ(count, 1, "whitespace only should produce 1 token (EOF)");
    ASSERT_EQ(toks[0].kind, TOK_EOF, "should be EOF");
    free(toks);
    PASS();
}

TEST(single_line_comment) {
    u32 count;
    Token *toks = lex_str("// this is a comment\n42", &count);
    ASSERT_EQ(count, 2, "comment + number + EOF");
    ASSERT_EQ(toks[0].kind, TOK_INT_LITERAL, "first real token should be INT");
    ASSERT_EQ(toks[0].int_lit.value, 42, "value should be 42");
    free(toks);
    PASS();
}

TEST(block_comment) {
    u32 count;
    Token *toks = lex_str("/* comment */ 42", &count);
    ASSERT_EQ(count, 2, "block comment + number + EOF");
    ASSERT_EQ(toks[0].kind, TOK_INT_LITERAL, "should be INT");
    free(toks);
    PASS();
}

TEST(nested_block_comment) {
    u32 count;
    Token *toks = lex_str("/* outer /* inner */ still comment */ 99", &count);
    ASSERT_EQ(count, 2, "nested comment + number + EOF");
    ASSERT_EQ(toks[0].kind, TOK_INT_LITERAL, "should be INT");
    ASSERT_EQ(toks[0].int_lit.value, 99, "value should be 99");
    free(toks);
    PASS();
}

TEST(keywords) {
    /* Test a selection of keywords */
    struct { const char *code; TokenKind expected; } cases[] = {
        { "fn",        TOK_KW_FN },
        { "let",       TOK_KW_LET },
        { "const",     TOK_KW_CONST },
        { "mut",       TOK_KW_MUT },
        { "struct",    TOK_KW_STRUCT },
        { "enum",      TOK_KW_ENUM },
        { "if",        TOK_KW_IF },
        { "else",      TOK_KW_ELSE },
        { "match",     TOK_KW_MATCH },
        { "for",       TOK_KW_FOR },
        { "while",     TOK_KW_WHILE },
        { "loop",      TOK_KW_LOOP },
        { "return",    TOK_KW_RETURN },
        { "unsafe",    TOK_KW_UNSAFE },
        { "driver",    TOK_KW_DRIVER },
        { "syscall",   TOK_KW_SYSCALL },
        { "interrupt", TOK_KW_INTERRUPT },
        { "pub",       TOK_KW_PUB },
        { "import",    TOK_KW_IMPORT },
        { "module",    TOK_KW_MODULE },
        { "u8",        TOK_KW_U8 },
        { "u16",       TOK_KW_U16 },
        { "u32",       TOK_KW_U32 },
        { "u64",       TOK_KW_U64 },
        { "i32",       TOK_KW_I32 },
        { "bool",      TOK_KW_BOOL },
        { "void",      TOK_KW_VOID },
        { "true",      TOK_KW_TRUE },
        { "false",     TOK_KW_FALSE },
        { "null",      TOK_KW_NULL },
        { "self",      TOK_KW_SELF },
        { "Self",      TOK_KW_SELF_TYPE },
        { "and",       TOK_KW_AND },
        { "or",        TOK_KW_OR },
        { "not",       TOK_KW_NOT },
        { "ptr",       TOK_KW_PTR },
        { "own",       TOK_KW_OWN },
        { "defer",     TOK_KW_DEFER },
        { "asm",       TOK_KW_ASM },
        { "mmio",      TOK_KW_MMIO },
        { "port",      TOK_KW_PORT },
    };

    for (usize i = 0; i < ARRAY_LEN(cases); i++) {
        u32 count;
        Token *toks = lex_str(cases[i].code, &count);
        if (toks[0].kind != cases[i].expected) {
            char buf[128];
            snprintf(buf, sizeof(buf), "keyword '%s' expected %s got %s",
                     cases[i].code,
                     token_kind_to_str(cases[i].expected),
                     token_kind_to_str(toks[0].kind));
            free(toks);
            FAIL(buf);
        }
        free(toks);
    }
    PASS();
}

TEST(identifiers) {
    u32 count;
    Token *toks = lex_str("my_var PageTable _private x123", &count);
    ASSERT_EQ(count, 5, "4 identifiers + EOF");
    ASSERT_EQ(toks[0].kind, TOK_IDENTIFIER, "first should be IDENT");
    ASSERT_TRUE(toks[0].ident.length == 6, "my_var has 6 chars");
    ASSERT_EQ(toks[1].kind, TOK_IDENTIFIER, "second should be IDENT");
    ASSERT_EQ(toks[2].kind, TOK_IDENTIFIER, "third should be IDENT");
    ASSERT_EQ(toks[3].kind, TOK_IDENTIFIER, "fourth should be IDENT");
    free(toks);
    PASS();
}

TEST(int_decimal) {
    u32 count;
    Token *toks = lex_str("42 0 1000000 1_000_000", &count);
    ASSERT_EQ(count, 5, "4 ints + EOF");
    ASSERT_EQ(toks[0].int_lit.value, 42, "42");
    ASSERT_EQ(toks[1].int_lit.value, 0, "0");
    ASSERT_EQ(toks[2].int_lit.value, 1000000, "1000000");
    ASSERT_EQ(toks[3].int_lit.value, 1000000, "1_000_000");
    ASSERT_EQ(toks[0].int_lit.base, INT_BASE_DEC, "base 10");
    free(toks);
    PASS();
}

TEST(int_hex) {
    u32 count;
    Token *toks = lex_str("0xFF 0x1A2B 0xDEAD_BEEF", &count);
    ASSERT_EQ(count, 4, "3 hex ints + EOF");
    ASSERT_EQ(toks[0].int_lit.value, 0xFF, "0xFF");
    ASSERT_EQ(toks[1].int_lit.value, 0x1A2B, "0x1A2B");
    ASSERT_EQ(toks[2].int_lit.value, 0xDEADBEEF, "0xDEAD_BEEF");
    ASSERT_EQ(toks[0].int_lit.base, INT_BASE_HEX, "base 16");
    free(toks);
    PASS();
}

TEST(int_binary) {
    u32 count;
    Token *toks = lex_str("0b1010 0b1111_0000", &count);
    ASSERT_EQ(count, 3, "2 bin ints + EOF");
    ASSERT_EQ(toks[0].int_lit.value, 0b1010, "0b1010 = 10");
    ASSERT_EQ(toks[1].int_lit.value, 0xF0, "0b1111_0000 = 0xF0");
    ASSERT_EQ(toks[0].int_lit.base, INT_BASE_BIN, "base 2");
    free(toks);
    PASS();
}

TEST(int_octal) {
    u32 count;
    Token *toks = lex_str("0o77 0o755", &count);
    ASSERT_EQ(count, 3, "2 oct ints + EOF");
    ASSERT_EQ(toks[0].int_lit.value, 077, "0o77 = 63");
    ASSERT_EQ(toks[1].int_lit.value, 0755, "0o755 = 493");
    ASSERT_EQ(toks[0].int_lit.base, INT_BASE_OCT, "base 8");
    free(toks);
    PASS();
}

TEST(float_literals) {
    u32 count;
    Token *toks = lex_str("3.14 1.0 0.5 1.0e10 2.5e-3", &count);
    ASSERT_EQ(count, 6, "5 floats + EOF");
    ASSERT_EQ(toks[0].kind, TOK_FLOAT_LITERAL, "3.14 is float");
    ASSERT_EQ(toks[1].kind, TOK_FLOAT_LITERAL, "1.0 is float");
    ASSERT_EQ(toks[2].kind, TOK_FLOAT_LITERAL, "0.5 is float");
    ASSERT_EQ(toks[3].kind, TOK_FLOAT_LITERAL, "1.0e10 is float");
    ASSERT_EQ(toks[4].kind, TOK_FLOAT_LITERAL, "2.5e-3 is float");
    /* Check approximate values */
    ASSERT_TRUE(toks[0].float_lit > 3.13 && toks[0].float_lit < 3.15, "3.14 value");
    free(toks);
    PASS();
}

TEST(string_literal) {
    u32 count;
    Token *toks = lex_str("\"hello world\"", &count);
    ASSERT_EQ(count, 2, "1 string + EOF");
    ASSERT_EQ(toks[0].kind, TOK_STRING_LITERAL, "should be STRING");
    ASSERT_EQ(toks[0].str_lit.length, 11, "hello world = 11 chars");
    free(toks);
    PASS();
}

TEST(string_with_escapes) {
    u32 count;
    Token *toks = lex_str("\"hello\\nworld\"", &count);
    ASSERT_EQ(count, 2, "1 string + EOF");
    ASSERT_EQ(toks[0].kind, TOK_STRING_LITERAL, "should be STRING");
    free(toks);
    PASS();
}

TEST(raw_string) {
    u32 count;
    Token *toks = lex_str("r\"no \\escapes here\"", &count);
    ASSERT_EQ(count, 2, "1 raw string + EOF");
    ASSERT_EQ(toks[0].kind, TOK_RAW_STRING_LITERAL, "should be RAW_STRING");
    ASSERT_TRUE(toks[0].str_lit.is_raw, "should be marked raw");
    free(toks);
    PASS();
}

TEST(char_literal) {
    u32 count;
    Token *toks = lex_str("'A' '\\n' '\\x41'", &count);
    ASSERT_EQ(count, 4, "3 chars + EOF");
    ASSERT_EQ(toks[0].kind, TOK_CHAR_LITERAL, "should be CHAR");
    ASSERT_EQ(toks[0].char_lit, 'A', "A = 0x41");
    ASSERT_EQ(toks[1].char_lit, '\n', "\\n = 0x0A");
    ASSERT_EQ(toks[2].char_lit, 0x41, "\\x41 = A");
    free(toks);
    PASS();
}

TEST(operators_arithmetic) {
    u32 count;
    Token *toks = lex_str("+ - * / %", &count);
    ASSERT_EQ(count, 6, "5 ops + EOF");
    ASSERT_EQ(toks[0].kind, TOK_PLUS, "+");
    ASSERT_EQ(toks[1].kind, TOK_MINUS, "-");
    ASSERT_EQ(toks[2].kind, TOK_STAR, "*");
    ASSERT_EQ(toks[3].kind, TOK_SLASH, "/");
    ASSERT_EQ(toks[4].kind, TOK_PERCENT, "%");
    free(toks);
    PASS();
}

TEST(operators_compound_assign) {
    u32 count;
    Token *toks = lex_str("+= -= *= /= %= &= |= ^= <<= >>=", &count);
    ASSERT_EQ(count, 11, "10 ops + EOF");
    ASSERT_EQ(toks[0].kind, TOK_PLUS_EQ, "+=");
    ASSERT_EQ(toks[1].kind, TOK_MINUS_EQ, "-=");
    ASSERT_EQ(toks[2].kind, TOK_STAR_EQ, "*=");
    ASSERT_EQ(toks[3].kind, TOK_SLASH_EQ, "/=");
    ASSERT_EQ(toks[4].kind, TOK_PERCENT_EQ, "%=");
    ASSERT_EQ(toks[5].kind, TOK_AMP_EQ, "&=");
    ASSERT_EQ(toks[6].kind, TOK_PIPE_EQ, "|=");
    ASSERT_EQ(toks[7].kind, TOK_CARET_EQ, "^=");
    ASSERT_EQ(toks[8].kind, TOK_SHL_EQ, "<<=");
    ASSERT_EQ(toks[9].kind, TOK_SHR_EQ, ">>=");
    free(toks);
    PASS();
}

TEST(operators_comparison) {
    u32 count;
    Token *toks = lex_str("== != < > <= >=", &count);
    ASSERT_EQ(count, 7, "6 ops + EOF");
    ASSERT_EQ(toks[0].kind, TOK_EQ_EQ, "==");
    ASSERT_EQ(toks[1].kind, TOK_NOT_EQ, "!=");
    ASSERT_EQ(toks[2].kind, TOK_LT, "<");
    ASSERT_EQ(toks[3].kind, TOK_GT, ">");
    ASSERT_EQ(toks[4].kind, TOK_LT_EQ, "<=");
    ASSERT_EQ(toks[5].kind, TOK_GT_EQ, ">=");
    free(toks);
    PASS();
}

TEST(operators_special) {
    u32 count;
    Token *toks = lex_str("-> => .. ..= :: ? @ ! << >>", &count);
    ASSERT_EQ(count, 11, "10 ops + EOF");
    ASSERT_EQ(toks[0].kind, TOK_ARROW, "->");
    ASSERT_EQ(toks[1].kind, TOK_FAT_ARROW, "=>");
    ASSERT_EQ(toks[2].kind, TOK_DOT_DOT, "..");
    ASSERT_EQ(toks[3].kind, TOK_DOT_DOT_EQ, "..=");
    ASSERT_EQ(toks[4].kind, TOK_COLON_COLON, "::");
    ASSERT_EQ(toks[5].kind, TOK_QUESTION, "?");
    ASSERT_EQ(toks[6].kind, TOK_AT, "@");
    ASSERT_EQ(toks[7].kind, TOK_BANG, "!");
    ASSERT_EQ(toks[8].kind, TOK_SHL, "<<");
    ASSERT_EQ(toks[9].kind, TOK_SHR, ">>");
    free(toks);
    PASS();
}

TEST(delimiters) {
    u32 count;
    Token *toks = lex_str("( ) { } [ ] , : ; . #", &count);
    ASSERT_EQ(count, 12, "11 delims + EOF");
    ASSERT_EQ(toks[0].kind, TOK_LPAREN, "(");
    ASSERT_EQ(toks[1].kind, TOK_RPAREN, ")");
    ASSERT_EQ(toks[2].kind, TOK_LBRACE, "{");
    ASSERT_EQ(toks[3].kind, TOK_RBRACE, "}");
    ASSERT_EQ(toks[4].kind, TOK_LBRACKET, "[");
    ASSERT_EQ(toks[5].kind, TOK_RBRACKET, "]");
    ASSERT_EQ(toks[6].kind, TOK_COMMA, ",");
    ASSERT_EQ(toks[7].kind, TOK_COLON, ":");
    ASSERT_EQ(toks[8].kind, TOK_SEMICOLON, ";");
    ASSERT_EQ(toks[9].kind, TOK_DOT, ".");
    ASSERT_EQ(toks[10].kind, TOK_HASH, "#");
    free(toks);
    PASS();
}

TEST(function_declaration) {
    u32 count;
    Token *toks = lex_str("fn add(a: i32, b: i32) -> i32 { return a + b; }", &count);
    /* fn add ( a : i32 , b : i32 ) -> i32 { return a + b ; } EOF */
    ASSERT_EQ(toks[0].kind, TOK_KW_FN, "fn");
    ASSERT_EQ(toks[1].kind, TOK_IDENTIFIER, "add");
    ASSERT_EQ(toks[2].kind, TOK_LPAREN, "(");
    ASSERT_EQ(toks[3].kind, TOK_IDENTIFIER, "a");
    ASSERT_EQ(toks[4].kind, TOK_COLON, ":");
    ASSERT_EQ(toks[5].kind, TOK_KW_I32, "i32");
    ASSERT_EQ(toks[6].kind, TOK_COMMA, ",");
    ASSERT_EQ(toks[7].kind, TOK_IDENTIFIER, "b");
    ASSERT_EQ(toks[8].kind, TOK_COLON, ":");
    ASSERT_EQ(toks[9].kind, TOK_KW_I32, "i32");
    ASSERT_EQ(toks[10].kind, TOK_RPAREN, ")");
    ASSERT_EQ(toks[11].kind, TOK_ARROW, "->");
    ASSERT_EQ(toks[12].kind, TOK_KW_I32, "i32");
    ASSERT_EQ(toks[13].kind, TOK_LBRACE, "{");
    ASSERT_EQ(toks[14].kind, TOK_KW_RETURN, "return");
    ASSERT_EQ(toks[15].kind, TOK_IDENTIFIER, "a");
    ASSERT_EQ(toks[16].kind, TOK_PLUS, "+");
    ASSERT_EQ(toks[17].kind, TOK_IDENTIFIER, "b");
    ASSERT_EQ(toks[18].kind, TOK_SEMICOLON, ";");
    ASSERT_EQ(toks[19].kind, TOK_RBRACE, "}");
    ASSERT_EQ(toks[20].kind, TOK_EOF, "EOF");
    free(toks);
    PASS();
}

TEST(struct_declaration) {
    u32 count;
    Token *toks = lex_str("pub struct Point { x: i32, y: i32, }", &count);
    ASSERT_EQ(toks[0].kind, TOK_KW_PUB, "pub");
    ASSERT_EQ(toks[1].kind, TOK_KW_STRUCT, "struct");
    ASSERT_EQ(toks[2].kind, TOK_IDENTIFIER, "Point");
    ASSERT_EQ(toks[3].kind, TOK_LBRACE, "{");
    free(toks);
    PASS();
}

TEST(unsafe_block) {
    u32 count;
    Token *toks = lex_str("unsafe { port::out_u8(0x20, 0x11); }", &count);
    ASSERT_EQ(toks[0].kind, TOK_KW_UNSAFE, "unsafe");
    ASSERT_EQ(toks[1].kind, TOK_LBRACE, "{");
    ASSERT_EQ(toks[2].kind, TOK_KW_PORT, "port");
    ASSERT_EQ(toks[3].kind, TOK_COLON_COLON, "::");
    ASSERT_EQ(toks[4].kind, TOK_IDENTIFIER, "out_u8");
    ASSERT_EQ(toks[5].kind, TOK_LPAREN, "(");
    ASSERT_EQ(toks[6].kind, TOK_INT_LITERAL, "0x20");
    ASSERT_EQ(toks[6].int_lit.value, 0x20, "value 0x20");
    ASSERT_EQ(toks[6].int_lit.base, INT_BASE_HEX, "hex base");
    free(toks);
    PASS();
}

TEST(driver_construct) {
    u32 count;
    Token *toks = lex_str("driver PS2Keyboard { buffer: RingBuffer<u8, 256>, }", &count);
    ASSERT_EQ(toks[0].kind, TOK_KW_DRIVER, "driver");
    ASSERT_EQ(toks[1].kind, TOK_IDENTIFIER, "PS2Keyboard");
    ASSERT_EQ(toks[2].kind, TOK_LBRACE, "{");
    ASSERT_EQ(toks[3].kind, TOK_IDENTIFIER, "buffer");
    ASSERT_EQ(toks[4].kind, TOK_COLON, ":");
    ASSERT_EQ(toks[5].kind, TOK_IDENTIFIER, "RingBuffer");
    ASSERT_EQ(toks[6].kind, TOK_LT, "<");
    ASSERT_EQ(toks[7].kind, TOK_KW_U8, "u8");
    ASSERT_EQ(toks[8].kind, TOK_COMMA, ",");
    ASSERT_EQ(toks[9].kind, TOK_INT_LITERAL, "256");
    ASSERT_EQ(toks[10].kind, TOK_GT, ">");
    free(toks);
    PASS();
}

TEST(range_not_float) {
    /* Test that 0..10 produces INT DOT_DOT INT, not FLOAT */
    u32 count;
    Token *toks = lex_str("0..10", &count);
    ASSERT_EQ(count, 4, "3 tokens + EOF");
    ASSERT_EQ(toks[0].kind, TOK_INT_LITERAL, "0 is INT");
    ASSERT_EQ(toks[1].kind, TOK_DOT_DOT, "..");
    ASSERT_EQ(toks[2].kind, TOK_INT_LITERAL, "10 is INT");
    free(toks);
    PASS();
}

TEST(attribute) {
    u32 count;
    Token *toks = lex_str("#[no_mangle]", &count);
    ASSERT_EQ(toks[0].kind, TOK_HASH, "#");
    ASSERT_EQ(toks[1].kind, TOK_LBRACKET, "[");
    ASSERT_EQ(toks[2].kind, TOK_IDENTIFIER, "no_mangle");
    ASSERT_EQ(toks[3].kind, TOK_RBRACKET, "]");
    free(toks);
    PASS();
}

TEST(error_unterminated_string) {
    u32 count;
    Token *toks = lex_str("\"hello", &count);
    ASSERT_EQ(toks[0].kind, TOK_ERROR, "should be ERROR for unterminated string");
    ASSERT_TRUE(diag_get_error_count() > 0, "should have error diagnostic");
    free(toks);
    PASS();
}

TEST(error_unexpected_char) {
    u32 count;
    Token *toks = lex_str("$", &count);
    ASSERT_EQ(toks[0].kind, TOK_ERROR, "should be ERROR for unexpected char");
    free(toks);
    PASS();
}

TEST(full_kernel_snippet) {
    const char *code =
        "module kernel;\n"
        "\n"
        "import kernel::drivers::vga { VGABuffer };\n"
        "\n"
        "#[no_mangle]\n"
        "pub fn _start() -> ! {\n"
        "    let vga = VGABuffer::get();\n"
        "    vga.clear(0x00);\n"
        "    loop {\n"
        "        unsafe { asm!(\"hlt\"); }\n"
        "    }\n"
        "}\n";

    u32 count;
    Token *toks = lex_str(code, &count);

    /* Should tokenize without errors */
    ASSERT_TRUE(diag_get_error_count() == 0, "should have no errors");
    ASSERT_TRUE(count > 30, "should produce many tokens");

    /* Check some key tokens */
    ASSERT_EQ(toks[0].kind, TOK_KW_MODULE, "module");
    ASSERT_EQ(toks[1].kind, TOK_IDENTIFIER, "kernel");
    ASSERT_EQ(toks[2].kind, TOK_SEMICOLON, ";");
    ASSERT_EQ(toks[3].kind, TOK_KW_IMPORT, "import");

    free(toks);
    PASS();
}

/* ── Main ────────────────────────────────────────────────────── */

int main(void) {
    printf("=== Q+ Lexer Test Suite ===\n\n");

    source_registry_init();

    run_test_empty_input();
    run_test_whitespace_only();
    run_test_single_line_comment();
    run_test_block_comment();
    run_test_nested_block_comment();
    run_test_keywords();
    run_test_identifiers();
    run_test_int_decimal();
    run_test_int_hex();
    run_test_int_binary();
    run_test_int_octal();
    run_test_float_literals();
    run_test_string_literal();
    run_test_string_with_escapes();
    run_test_raw_string();
    run_test_char_literal();
    run_test_operators_arithmetic();
    run_test_operators_compound_assign();
    run_test_operators_comparison();
    run_test_operators_special();
    run_test_delimiters();
    run_test_function_declaration();
    run_test_struct_declaration();
    run_test_unsafe_block();
    run_test_driver_construct();
    run_test_range_not_float();
    run_test_attribute();
    run_test_error_unterminated_string();
    run_test_error_unexpected_char();
    run_test_full_kernel_snippet();

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf(", %d FAILED", g_tests_failed);
    }
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
