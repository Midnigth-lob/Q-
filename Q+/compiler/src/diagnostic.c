/*
 * Q+ Compiler (qpc) — Diagnostic System (Implementation)
 */

#include "qpc/diagnostic.h"

/* ── Counters ────────────────────────────────────────────────── */
static u32 g_error_count = 0;
static u32 g_warning_count = 0;

/* ── ANSI Color codes ────────────────────────────────────────── */
#ifdef _WIN32
#include <windows.h>
static void enable_ansi(void) {
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#define COL_RESET   "\033[0m"
#define COL_RED     "\033[1;31m"
#define COL_YELLOW  "\033[1;33m"
#define COL_CYAN    "\033[1;36m"
#define COL_WHITE   "\033[1;37m"
#define COL_GREEN   "\033[1;32m"
#define COL_BLUE    "\033[1;34m"
#define COL_DIM     "\033[2m"
#else
#define enable_ansi() ((void)0)
#define COL_RESET   "\033[0m"
#define COL_RED     "\033[1;31m"
#define COL_YELLOW  "\033[1;33m"
#define COL_CYAN    "\033[1;36m"
#define COL_WHITE   "\033[1;37m"
#define COL_GREEN   "\033[1;32m"
#define COL_BLUE    "\033[1;34m"
#define COL_DIM     "\033[2m"
#endif

static bool g_ansi_enabled = false;

static void ensure_ansi(void) {
    if (!g_ansi_enabled) {
        enable_ansi();
        g_ansi_enabled = true;
    }
}

/* ── Level to string/color ───────────────────────────────────── */
static const char *level_str(DiagLevel level) {
    switch (level) {
        case DIAG_ERROR:   return "error";
        case DIAG_WARNING: return "warning";
        case DIAG_NOTE:    return "note";
    }
    return "unknown";
}

static const char *level_color(DiagLevel level) {
    switch (level) {
        case DIAG_ERROR:   return COL_RED;
        case DIAG_WARNING: return COL_YELLOW;
        case DIAG_NOTE:    return COL_CYAN;
    }
    return COL_WHITE;
}

/* ── Emit diagnostic with source location ────────────────────── */
void diag_emit(DiagLevel level, u16 source_id, Span span,
               const char *fmt, ...) {
    ensure_ansi();

    if (level == DIAG_ERROR) g_error_count++;
    if (level == DIAG_WARNING) g_warning_count++;

    SourceFile *src = source_get(source_id);
    SourceLoc loc = { 1, 1 };
    if (src) {
        loc = source_get_loc(src, span.start);
    }

    /* Header: path:line:col: level: message */
    const char *path = src ? src->path : "<unknown>";
    fprintf(stderr, "%s%s:%u:%u:%s %s%s%s: ",
            COL_WHITE, path, loc.line, loc.col, COL_RESET,
            level_color(level), level_str(level), COL_RESET);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    /* Source line display */
    if (src) {
        StringView line_text = source_get_line_text(src, loc.line);
        if (line_text.length > 0) {
            /* Line number gutter */
            fprintf(stderr, " %s%5u |%s ", COL_BLUE, loc.line, COL_RESET);

            /* Print the line */
            fprintf(stderr, "%.*s\n", (int)line_text.length, line_text.data);

            /* Caret line */
            fprintf(stderr, "       %s|%s ", COL_BLUE, COL_RESET);

            /* Spaces up to the column */
            for (u32 i = 1; i < loc.col; i++) {
                fputc(' ', stderr);
            }

            /* Caret(s) under the span */
            u32 span_len = span.end > span.start ? span.end - span.start : 1;
            /* Clamp to remaining line length */
            u32 max_carets = (u32)line_text.length - (loc.col - 1);
            if (span_len > max_carets) span_len = max_carets;
            if (span_len == 0) span_len = 1;

            fprintf(stderr, "%s", level_color(level));
            for (u32 i = 0; i < span_len; i++) {
                fputc('^', stderr);
            }
            fprintf(stderr, "%s\n", COL_RESET);
        }
    }
}

/* ── Emit simple diagnostic (no source) ──────────────────────── */
void diag_emit_simple(DiagLevel level, const char *fmt, ...) {
    ensure_ansi();

    if (level == DIAG_ERROR) g_error_count++;
    if (level == DIAG_WARNING) g_warning_count++;

    fprintf(stderr, "%s%s%s: ", level_color(level), level_str(level), COL_RESET);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/* ── Counters ────────────────────────────────────────────────── */
u32 diag_get_error_count(void) {
    return g_error_count;
}

u32 diag_get_warning_count(void) {
    return g_warning_count;
}

void diag_reset(void) {
    g_error_count = 0;
    g_warning_count = 0;
}
