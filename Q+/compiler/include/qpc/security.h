/*
 * Q+ Compiler (qpc) — Security Analyzer
 *
 * Performs 12 static analysis passes over the AST to detect
 * low-level vulnerabilities and unsafe patterns before compilation.
 * All issues are reported via the diagnostic system as errors or warnings.
 */

#ifndef QPC_SECURITY_H
#define QPC_SECURITY_H

#include "qpc/ast.h"

typedef struct {
    Arena *arena;
    i32    source_id;
    u32    error_count;
    u32    warning_count;

    /* Context flags for passes */
    bool   in_unsafe;       /* currently inside unsafe {} */
    bool   in_interrupt_fn; /* currently inside interrupt fn */
    bool   in_driver;       /* currently inside driver { } */
    bool   in_syscall;      /* currently inside syscall { } */
    u32    static_mut_count; /* # of static mut accessed in current fn */
} SecurityAnalyzer;

void security_init(SecurityAnalyzer *sa, Arena *arena, i32 source_id);
bool security_analyze(SecurityAnalyzer *sa, AstNode *program);

#endif /* QPC_SECURITY_H */
