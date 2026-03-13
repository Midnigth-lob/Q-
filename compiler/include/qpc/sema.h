/*
 * Q+ Compiler (qpc) — Semantic Analyzer
 *
 * Two-pass analysis:
 *   Pass 1: Collect top-level declarations into global symbol table
 *   Pass 2: Resolve names, check types, detect duplicates / undefined symbols
 *
 * Uses a scope stack (chain of hash tables) for lexical scoping.
 */

#ifndef QPC_SEMA_H
#define QPC_SEMA_H

#include "qpc/ast.h"

/* ── Symbol kinds ────────────────────────────────────────────── */
typedef enum {
    SYM_FN,
    SYM_STRUCT,
    SYM_ENUM,
    SYM_DRIVER,
    SYM_SYSCALL,
    SYM_CONST,
    SYM_STATIC,
    SYM_LOCAL,
    SYM_PARAM,
    SYM_TYPE_ALIAS,
    SYM_TRAIT,
    SYM_MODULE,
} SymKind;

/* ── Symbol entry ────────────────────────────────────────────── */
typedef struct Symbol {
    StringView  name;
    SymKind     kind;
    AstNode    *decl;    /* original declaration node */
    AstType    *type;    /* resolved type (NULL = unresolved) */
    bool        used;    /* for unused-variable detection */
    struct Symbol *next; /* hash chain */
} Symbol;

/* ── Symbol table (open-address chained hash map) ────────────── */
#define SYMTAB_BUCKETS 64
typedef struct SymTab {
    Symbol      *buckets[SYMTAB_BUCKETS];
    struct SymTab *parent;  /* enclosing scope */
} SymTab;

/* ── Sema context ────────────────────────────────────────────── */
typedef struct Sema {
    Arena    *arena;
    i32       source_id;
    u32       error_count;
    u32       warning_count;
    SymTab   *scope;          /* current innermost scope */
    SymTab   *global;         /* top-level module scope */
    AstType  *current_ret;    /* return type of current function */
    bool      in_loop;        /* for break/continue validation */
    bool      in_unsafe;      /* current unsafe context */
} Sema;

/* ── Public API ──────────────────────────────────────────────── */
void sema_init(Sema *sema, Arena *arena, i32 source_id);
bool sema_analyze(Sema *sema, AstNode *program);

/* Symbol table helpers (used by security.c integration tests) */
Symbol *symtab_lookup(SymTab *tab, StringView name);
void    symtab_insert(Sema *s, StringView name, SymKind kind, AstNode *decl, AstType *type);
void    sema_push_scope(Sema *s);
void    sema_pop_scope(Sema *s);

#endif /* QPC_SEMA_H */
