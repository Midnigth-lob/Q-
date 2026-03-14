/*
 * Q+ Compiler (qpc) — Main Entry Point
 *
 * Usage:
 *   qpc lex <file.qp>       — Tokenize a file and print all tokens
 *   qpc parse <file.qp>     — Parse a file and print the AST
 *   qpc build <file.qp>     — Compile a Q+ project (future)
 *   qpc --version            — Print version
 *   qpc --help               — Print usage
 */

#include "qpc/common.h"
#include "qpc/token.h"
#include "qpc/source.h"
#include "qpc/lexer.h"
#include "qpc/parser.h"
#include "qpc/ast.h"
#include "qpc/sema.h"
#include "qpc/security.h"
#include "qpc/codegen.h"
#include "qpc/diagnostic.h"

static void print_usage(void) {
    fprintf(stderr,
        "Q+ Compiler (qpc) v" QPC_VERSION_STRING "\n"
        "\n"
        "Usage:\n"
        "  qpc lex <file.qp>       Tokenize a file and print all tokens\n"
        "  qpc parse <file.qp>     Parse a file and print the AST\n"
        "  qpc build <file.qp>     Compile a Q+ project to output.c\n"
        "  qpc --version            Print version\n"
        "  qpc --help               Print this help message\n"
        "\n"
    );
}

static void print_version(void) {
    printf("qpc %s\n", QPC_VERSION_STRING);
}

/* ── Lex subcommand ──────────────────────────────────────────── */
static int cmd_lex(const char *path) {
    source_registry_init();
    diag_reset();

    i32 src_id = source_load(path);
    if (src_id < 0) return 1;

    SourceFile *src = source_get((u16)src_id);
    Lexer lex;
    lexer_init(&lex, src);

    printf("=== Lexing: %s (%zu bytes) ===\n\n", path, src->length);
    u32 count = 0;
    for (;;) {
        Token tok = lexer_next_token(&lex);
        token_print(&tok, src->content);
        count++;
        if (tok.kind == TOK_EOF) break;
    }
    printf("\n=== %u tokens", count);
    if (diag_get_error_count() > 0) printf(", %u error(s)", diag_get_error_count());
    if (diag_get_warning_count() > 0) printf(", %u warning(s)", diag_get_warning_count());
    printf(" ===\n");
    return diag_get_error_count() > 0 ? 1 : 0;
}

/* ── Parse subcommand ────────────────────────────────────────── */
static int cmd_parse(const char *path) {
    source_registry_init();
    diag_reset();

    i32 src_id = source_load(path);
    if (src_id < 0) return 1;

    SourceFile *src = source_get((u16)src_id);
    Lexer lex;
    lexer_init(&lex, src);

    Arena arena;
    arena_init(&arena);

    Parser parser;
    parser_init(&parser, &lex, &arena);

    printf("=== Parsing: %s ===\n\n", path);
    AstNode *program = parser_parse(&parser);
    ast_print(program, 0);

    printf("\n=== %u declarations", program->program.decl_count);
    if (diag_get_error_count() > 0) printf(", %u error(s)", diag_get_error_count());
    if (diag_get_warning_count() > 0) printf(", %u warning(s)", diag_get_warning_count());
    printf(" ===\n");

    arena_free(&arena);
    return diag_get_error_count() > 0 ? 1 : 0;
}

/* ── Build subcommand ────────────────────────────────────────── */
static int cmd_build(const char *path) {
    source_registry_init();
    diag_reset();

    i32 src_id = source_load(path);
    if (src_id < 0) return 1;

    SourceFile *src = source_get((u16)src_id);
    Lexer lex;
    lexer_init(&lex, src);

    Arena arena;
    arena_init(&arena);

    Parser parser;
    parser_init(&parser, &lex, &arena);

    printf("=== Building: %s ===\n", path);
    AstNode *program = parser_parse(&parser);

    if (diag_get_error_count() > 0) {
        printf("Parse failed with %u errors.\n", diag_get_error_count());
        arena_free(&arena);
        return 1;
    }

    Sema sema;
    sema_init(&sema, &arena, src_id);
    if (!sema_analyze(&sema, program) || diag_get_error_count() > 0) {
        printf("Semantic analysis failed with %u errors.\n", diag_get_error_count());
        arena_free(&arena);
        return 1;
    }
    if (sema.warning_count > 0)
        printf("Sema: %u warning(s)\n", sema.warning_count);

    SecurityAnalyzer sa;
    security_init(&sa, &arena, src_id);
    if (!security_analyze(&sa, program)) {
        printf("Security analysis found %u error(s).\n", sa.error_count);
        if (sa.warning_count > 0)
            printf("Security: %u warning(s)\n", sa.warning_count);
        arena_free(&arena);
        return 1;
    }
    if (sa.warning_count > 0)
        printf("Security: %u warning(s)\n", sa.warning_count);

    const char *out_file = "output.c";
    if (!codegen_emit_c(program, out_file)) {
        printf("Codegen failed.\n");
        arena_free(&arena);
        return 1;
    }

    printf("Successfully built -> %s\n", out_file);
    printf("  Parse:    OK\n");
    printf("  Sema:     OK (%u warning(s))\n", sema.warning_count);
    printf("  Security: OK (%u warning(s))\n", sa.warning_count);
    printf("  Codegen:  OK -> %s\n", out_file);
    arena_free(&arena);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 1; }
    const char *cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) { print_usage(); return 0; }
    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) { print_version(); return 0; }

    if (strcmp(cmd, "lex") == 0) {
        if (argc < 3) { fprintf(stderr, "error: 'lex' requires a file path\n"); return 1; }
        return cmd_lex(argv[2]);
    }
    if (strcmp(cmd, "parse") == 0) {
        if (argc < 3) { fprintf(stderr, "error: 'parse' requires a file path\n"); return 1; }
        return cmd_parse(argv[2]);
    }
    if (strcmp(cmd, "build") == 0) {
        if (argc < 3) { fprintf(stderr, "error: 'build' requires a file path\n"); return 1; }
        return cmd_build(argv[2]);
    }
    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    print_usage();
    return 1;
}
