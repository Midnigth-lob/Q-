/*
 * Q+ Compiler (qpc) — C Code Generator
 *
 * Translates a Q+ AST to portable, freestanding C (C11).
 * The generated C targets bare-metal environments:
 *   - No C standard library inclusion (unless the module imports it)
 *   - Fixed-width types via <stdint.h> / <stdbool.h>
 *   - Compiler attributes for interrupt handlers, packed structs, etc.
 *   - Inline asm for port I/O, halt, and low-level instructions
 *
 * Usage:
 *   bool codegen_emit_c(AstNode *program, const char *out_path);
 */

#ifndef QPC_CODEGEN_H
#define QPC_CODEGEN_H

#include "qpc/ast.h"
#include <stdio.h>

/* ── Code Writer ─────────────────────────────────────────────── */

/*
 * CWriter — a growable text buffer used during code generation.
 * Writes to a FILE* when flush()-ed.
 */
typedef struct CWriter {
    char  *buf;
    usize  len;
    usize  cap;
    int    indent;      /* current indent level (in spaces of 4) */
} CWriter;

void cw_init(CWriter *cw);
void cw_free(CWriter *cw);
void cw_write(CWriter *cw, const char *s);
void cw_writef(CWriter *cw, const char *fmt, ...);
void cw_writeln(CWriter *cw, const char *s);
void cw_indent(CWriter *cw);
void cw_dedent(CWriter *cw);
void cw_nl(CWriter *cw);
bool cw_flush_to_file(CWriter *cw, const char *path);

/* ── Code Generator ──────────────────────────────────────────── */

typedef struct CodeGen {
    Arena   *arena;
    CWriter  out;
    /* Defer stack: tracks deferred expressions per scope */
    AstNode *defer_stack[256];
    u32      defer_top;
} CodeGen;

void codegen_init(CodeGen *cg, Arena *arena);
void codegen_free(CodeGen *cg);

/* Top-level entry point */
bool codegen_emit_c(AstNode *program, const char *out_path);

#endif /* QPC_CODEGEN_H */
