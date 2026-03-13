/*
 * Q+ Compiler (qpc) — Diagnostic System
 *
 * Error, warning, and note reporting with source location display.
 */

#ifndef QPC_DIAGNOSTIC_H
#define QPC_DIAGNOSTIC_H

#include "common.h"
#include "token.h"
#include "source.h"

typedef enum DiagLevel {
    DIAG_NOTE,
    DIAG_WARNING,
    DIAG_ERROR,
} DiagLevel;

/* Emit a diagnostic message with source location and caret pointer */
void diag_emit(DiagLevel level, u16 source_id, Span span,
               const char *fmt, ...);

/* Emit a simple diagnostic without source location */
void diag_emit_simple(DiagLevel level, const char *fmt, ...);

/* Get current error count */
u32 diag_get_error_count(void);

/* Get current warning count */
u32 diag_get_warning_count(void);

/* Reset counters */
void diag_reset(void);

#endif /* QPC_DIAGNOSTIC_H */
