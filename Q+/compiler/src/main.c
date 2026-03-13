/*
 * Q+ Compiler (qpc) — Main Entry Point
 *
 * Usage:
 *   qpc lex <file.qp>       — Tokenize a file and print all tokens
 *   qpc build <file.qp>     — Compile a Q+ project (future)
 *   qpc --version            — Print version
 *   qpc --help               — Print usage
 */

#include "qpc/common.h"
#include "qpc/token.h"
#include "qpc/source.h"
#include "qpc/lexer.h"
#include "qpc/diagnostic.h"

static void print_usage(void) {
    fprintf(stderr,
        "Q+ Compiler (qpc) v" QPC_VERSION_STRING "\n"
        "\n"
        "Usage:\n"
        "  qpc lex <file.qp>       Tokenize a file and print all tokens\n"
        "  qpc build <file.qp>     Compile a Q+ project (not yet implemented)\n"
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
    if (src_id < 0) {
        return 1;
    }

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
    if (diag_get_error_count() > 0) {
        printf(", %u error(s)", diag_get_error_count());
    }
    if (diag_get_warning_count() > 0) {
        printf(", %u warning(s)", diag_get_warning_count());
    }
    printf(" ===\n");

    return diag_get_error_count() > 0 ? 1 : 0;
}

/* ── Main ────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        print_version();
        return 0;
    }

    if (strcmp(cmd, "lex") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: 'lex' command requires a file path\n");
            print_usage();
            return 1;
        }
        return cmd_lex(argv[2]);
    }

    if (strcmp(cmd, "build") == 0) {
        fprintf(stderr, "error: 'build' command is not yet implemented\n");
        fprintf(stderr, "  The Q+ compiler is under construction.\n");
        fprintf(stderr, "  Currently available: qpc lex <file.qp>\n");
        return 1;
    }

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    print_usage();
    return 1;
}
