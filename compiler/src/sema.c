/*
 * Q+ Compiler (qpc) — Semantic Analyzer (sema.c)
 *
 * Two-pass: collect declarations, then resolve and type-check.
 */

#include "qpc/sema.h"
#include "qpc/diagnostic.h"
#include <string.h>

/* ── Symbol table hash ─────────────────────────────────────────── */

static u32 sv_hash(StringView sv) {
    u32 h = 2166136261u;
    for (usize i = 0; i < sv.length; i++)
        h = (h ^ (u8)sv.data[i]) * 16777619u;
    return h % SYMTAB_BUCKETS;
}

Symbol *symtab_lookup(SymTab *tab, StringView name) {
    for (SymTab *t = tab; t; t = t->parent) {
        u32 bucket = sv_hash(name);
        for (Symbol *s = t->buckets[bucket]; s; s = s->next)
            if (sv_equals(s->name, name)) return s;
    }
    return NULL;
}

static Symbol *symtab_lookup_local(SymTab *tab, StringView name) {
    if (!tab) return NULL;
    u32 bucket = sv_hash(name);
    for (Symbol *s = tab->buckets[bucket]; s; s = s->next)
        if (sv_equals(s->name, name)) return s;
    return NULL;
}

void symtab_insert(Sema *s, StringView name, SymKind kind, AstNode *decl, AstType *type) {
    u32 bucket = sv_hash(name);
    Symbol *sym = (Symbol *)arena_alloc(s->arena, sizeof(Symbol));
    sym->name = name;
    sym->kind = kind;
    sym->decl = decl;
    sym->type = type;
    sym->used = false;
    sym->next = s->scope->buckets[bucket];
    s->scope->buckets[bucket] = sym;
}

void sema_push_scope(Sema *s) {
    SymTab *tab = (SymTab *)arena_alloc(s->arena, sizeof(SymTab));
    memset(tab, 0, sizeof(SymTab));
    tab->parent = s->scope;
    s->scope = tab;
}

void sema_pop_scope(Sema *s) {
    if (s->scope) s->scope = s->scope->parent;
}

/* ── Errors/warnings ──────────────────────────────────────────── */

static void sema_error(Sema *s, Span span, const char *fmt, ...) {
    s->error_count++;
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    diag_emit(DIAG_ERROR, s->source_id, span, "%s", buf);
}

static void sema_warn(Sema *s, Span span, const char *fmt, ...) {
    s->warning_count++;
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    diag_emit(DIAG_WARNING, s->source_id, span, "%s", buf);
}

/* ── Forward decls ────────────────────────────────────────────── */
static void sema_expr(Sema *s, AstNode *n);
static void sema_stmt(Sema *s, AstNode *n);
static void sema_decl_pass1(Sema *s, AstNode *n);
static void sema_decl_pass2(Sema *s, AstNode *n);
static void sema_type(Sema *s, AstType *t);

/* ── Type analysis ────────────────────────────────────────────── */

static void sema_type(Sema *s, AstType *t) {
    if (!t) return;
    switch (t->kind) {
        case AST_TYPE_PRIMITIVE:
        case AST_TYPE_INFERRED:
        case AST_TYPE_NEVER:
            break;
        case AST_TYPE_NAMED: {
            /* Check that named type exists in scope */
            Symbol *sym = symtab_lookup(s->scope, t->named.name);
            if (!sym) {
                /* Allow well-known external types silently (Option, Result, etc.) */
                static const char *builtin[] = {
                    "Option", "Result", "Ok", "Err", "Some", "None",
                    "RingBuffer", "Vec", "InterruptFrame", "DriverError",
                    "PageTable", "PhysAddr", "VirtAddr", "CpuContext",
                };
                bool found = false;
                for (usize i = 0; i < sizeof(builtin)/sizeof(builtin[0]); i++) {
                    if (sv_equals_cstr(t->named.name, builtin[i])) { found = true; break; }
                }
                if (!found) {
                    sema_warn(s, t->span,
                        "type '%.*s' is not declared in this module (may be from an import)",
                        (int)t->named.name.length, t->named.name.data);
                }
            }
            break;
        }
        case AST_TYPE_PATH:    break;
        case AST_TYPE_ARRAY:   sema_type(s, t->array.element); break;
        case AST_TYPE_SLICE:   sema_type(s, t->slice.element); break;
        case AST_TYPE_REF:     sema_type(s, t->ref.inner);   break;
        case AST_TYPE_PTR:     sema_type(s, t->ptr.inner);   break;
        case AST_TYPE_OWN:     sema_type(s, t->own.inner);   break;
        case AST_TYPE_FN_PTR:
            for (u32 i = 0; i < t->fn_ptr.param_count; i++) sema_type(s, t->fn_ptr.params[i]);
            sema_type(s, t->fn_ptr.return_type);
            break;
        case AST_TYPE_TUPLE:
            for (u32 i = 0; i < t->tuple.count; i++) sema_type(s, t->tuple.elements[i]);
            break;
        case AST_TYPE_GENERIC:
            sema_type(s, t->generic.base);
            for (u32 i = 0; i < t->generic.arg_count; i++) sema_type(s, t->generic.args[i]);
            break;
    }
}

/* ── Expression analysis ──────────────────────────────────────── */

static void sema_expr(Sema *s, AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_INT_LIT: case AST_FLOAT_LIT: case AST_STRING_LIT:
        case AST_CHAR_LIT: case AST_BOOL_LIT: case AST_NULL_LIT:
            break;

        case AST_IDENT: {
            Symbol *sym = symtab_lookup(s->scope, n->ident.name);
            if (!sym) {
                /* Allow common built-in names */
                if (!sv_equals_cstr(n->ident.name, "_") &&
                    !sv_equals_cstr(n->ident.name, "cpu") &&
                    !sv_equals_cstr(n->ident.name, "port") &&
                    !sv_equals_cstr(n->ident.name, "mmio")) {
                    sema_warn(s, n->span,
                        "'%.*s' is not declared in this scope (may be from an import or built-in)",
                        (int)n->ident.name.length, n->ident.name.data);
                }
            } else {
                sym->used = true;
            }
            break;
        }

        case AST_PATH: break;  /* Module paths validated at link time */

        case AST_BINARY_OP:
            sema_expr(s, n->binary.left);
            sema_expr(s, n->binary.right);
            break;

        case AST_UNARY_OP:
            sema_expr(s, n->unary.operand);
            break;

        case AST_CALL:
            sema_expr(s, n->call.callee);
            for (u32 i = 0; i < n->call.arg_count; i++) sema_expr(s, n->call.args[i]);
            break;

        case AST_METHOD_CALL:
            sema_expr(s, n->method_call.object);
            for (u32 i = 0; i < n->method_call.arg_count; i++) sema_expr(s, n->method_call.args[i]);
            break;

        case AST_FIELD_ACCESS:
            sema_expr(s, n->field_access.object);
            break;

        case AST_INDEX:
            sema_expr(s, n->index.object);
            sema_expr(s, n->index.index);
            break;

        case AST_CAST:
            sema_expr(s, n->cast.expr);
            sema_type(s, n->cast.target_type);
            break;

        case AST_BLOCK: {
            sema_push_scope(s);
            for (u32 i = 0; i < n->block.stmt_count; i++) sema_stmt(s, n->block.stmts[i]);
            if (n->block.final_expr) sema_expr(s, n->block.final_expr);
            sema_pop_scope(s);
            break;
        }

        case AST_IF:
            sema_expr(s, n->if_expr.condition);
            sema_expr(s, n->if_expr.then_block);
            if (n->if_expr.else_block) sema_expr(s, n->if_expr.else_block);
            break;

        case AST_MATCH:
            sema_expr(s, n->match_expr.expr);
            for (u32 i = 0; i < n->match_expr.arm_count; i++) {
                AstNode *arm = n->match_expr.arms[i];
                if (arm->match_arm.guard) sema_expr(s, arm->match_arm.guard);
                sema_expr(s, arm->match_arm.body);
            }
            break;

        case AST_FOR: {
            sema_push_scope(s);
            symtab_insert(s, n->for_loop.var_name, SYM_LOCAL, n, NULL);
            sema_expr(s, n->for_loop.iterable);
            bool prev = s->in_loop; s->in_loop = true;
            sema_expr(s, n->for_loop.body);
            s->in_loop = prev;
            sema_pop_scope(s);
            break;
        }

        case AST_WHILE: {
            sema_expr(s, n->while_loop.condition);
            bool prev = s->in_loop; s->in_loop = true;
            sema_expr(s, n->while_loop.body);
            s->in_loop = prev;
            break;
        }

        case AST_LOOP: {
            bool prev = s->in_loop; s->in_loop = true;
            sema_expr(s, n->loop_expr.body);
            s->in_loop = prev;
            break;
        }

        case AST_UNSAFE_BLOCK: {
            bool prev = s->in_unsafe; s->in_unsafe = true;
            sema_expr(s, n->unsafe_block.body);
            s->in_unsafe = prev;
            break;
        }

        case AST_ARRAY_LIT:
            if (n->array_lit.elements)
                for (u32 i = 0; i < n->array_lit.element_count; i++) sema_expr(s, n->array_lit.elements[i]);
            if (n->array_lit.repeat_value) sema_expr(s, n->array_lit.repeat_value);
            if (n->array_lit.repeat_count) sema_expr(s, n->array_lit.repeat_count);
            break;

        case AST_STRUCT_LIT:
            for (u32 i = 0; i < n->struct_lit.field_count; i++) sema_expr(s, n->struct_lit.field_values[i]);
            break;

        case AST_ASM_EXPR:
            if (!s->in_unsafe)
                sema_error(s, n->span, "asm! is only allowed inside an 'unsafe {}' block");
            break;

        case AST_ERROR_PROP:
            sema_expr(s, n->error_prop.expr);
            break;

        default: break;
    }
}

/* ── Statement analysis ──────────────────────────────────────── */

static void sema_stmt(Sema *s, AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_LET_STMT: {
            /* Check for duplicate declaration in current scope */
            if (symtab_lookup_local(s->scope, n->let_stmt.name)) {
                sema_error(s, n->span,
                    "'%.*s' is already declared in this scope",
                    (int)n->let_stmt.name.length, n->let_stmt.name.data);
            }
            if (n->let_stmt.type) sema_type(s, n->let_stmt.type);
            if (n->let_stmt.init) sema_expr(s, n->let_stmt.init);
            symtab_insert(s, n->let_stmt.name, SYM_LOCAL, n, n->let_stmt.type);
            break;
        }
        case AST_ASSIGN_STMT:
            sema_expr(s, n->assign_stmt.target);
            sema_expr(s, n->assign_stmt.value);
            break;
        case AST_EXPR_STMT:  sema_expr(s, n->expr_stmt.expr); break;
        case AST_RETURN_STMT:
            if (n->return_stmt.value) sema_expr(s, n->return_stmt.value);
            break;
        case AST_BREAK_STMT:
        case AST_CONTINUE_STMT:
            if (!s->in_loop)
                sema_error(s, n->span, "break/continue used outside of a loop");
            break;
        case AST_DEFER_STMT:
            sema_expr(s, n->defer_stmt.expr);
            break;
        default: sema_decl_pass2(s, n); break;
    }
}

/* ── Declaration pass 1: register names in global scope ──────── */

static void sema_decl_pass1(Sema *s, AstNode *n) {
    if (!n) return;
    StringView name = {0};
    SymKind kind = SYM_FN;

    switch (n->kind) {
        case AST_FN_DECL:
            name = n->fn_decl.name;
            kind = SYM_FN;
            break;
        case AST_STRUCT_DECL:
            name = n->struct_decl.name;
            kind = SYM_STRUCT;
            break;
        case AST_ENUM_DECL:
            name = n->enum_decl.name;
            kind = SYM_ENUM;
            break;
        case AST_DRIVER_DECL:
            name = n->driver_decl.name;
            kind = SYM_DRIVER;
            break;
        case AST_SYSCALL_DECL:
            name = n->syscall_decl.name;
            kind = SYM_SYSCALL;
            break;
        case AST_CONST_DECL:
            name = n->const_decl.name;
            kind = SYM_CONST;
            break;
        case AST_STATIC_DECL:
            name = n->const_decl.name;
            kind = SYM_STATIC;
            break;
        case AST_TYPE_ALIAS:
            name = n->type_alias.name;
            kind = SYM_TYPE_ALIAS;
            break;
        default: return;
    }

    /* Detect duplicate top-level declarations */
    if (name.length > 0 && symtab_lookup_local(s->scope, name)) {
        sema_error(s, n->span,
            "duplicate declaration: '%.*s' is already defined in this module",
            (int)name.length, name.data);
        return;
    }
    if (name.length > 0)
        symtab_insert(s, name, kind, n, NULL);
}

/* ── Declaration pass 2: analyze bodies ───────────────────────── */

static void sema_decl_pass2(Sema *s, AstNode *n) {
    if (!n) return;
    switch (n->kind) {
        case AST_MODULE_DECL:
        case AST_IMPORT_DECL:
            break;

        case AST_FN_DECL: {
            sema_push_scope(s);
            for (u32 i = 0; i < n->fn_decl.param_count; i++) {
                AstParam *p = &n->fn_decl.params[i];
                if (p->type) sema_type(s, p->type);
                if (p->name.length > 0 && !sv_equals_cstr(p->name, "self"))
                    symtab_insert(s, p->name, SYM_PARAM, n, p->type);
            }
            AstType *prev_ret = s->current_ret;
            s->current_ret = n->fn_decl.return_type;
            if (n->fn_decl.return_type) sema_type(s, n->fn_decl.return_type);
            if (n->fn_decl.body) sema_expr(s, n->fn_decl.body);
            s->current_ret = prev_ret;
            sema_pop_scope(s);
            break;
        }

        case AST_STRUCT_DECL:
            for (u32 i = 0; i < n->struct_decl.field_count; i++)
                sema_type(s, n->struct_decl.fields[i].type);
            break;

        case AST_ENUM_DECL:
            if (n->enum_decl.repr_type) sema_type(s, n->enum_decl.repr_type);
            for (u32 i = 0; i < n->enum_decl.variant_count; i++) {
                if (n->enum_decl.variants[i].discriminant)
                    sema_expr(s, n->enum_decl.variants[i].discriminant);
            }
            break;

        case AST_CONST_DECL:
            sema_type(s, n->const_decl.type);
            sema_expr(s, n->const_decl.value);
            break;

        case AST_STATIC_DECL:
            sema_type(s, n->const_decl.type);
            if (n->const_decl.value) sema_expr(s, n->const_decl.value);
            break;

        case AST_TYPE_ALIAS:
            sema_type(s, n->type_alias.target);
            break;

        case AST_DRIVER_DECL: {
            /* Register driver type, then analyze methods */
            sema_push_scope(s);
            for (u32 i = 0; i < n->driver_decl.field_count; i++)
                sema_type(s, n->driver_decl.fields[i].type);
            for (u32 i = 0; i < n->driver_decl.method_count; i++)
                sema_decl_pass2(s, n->driver_decl.methods[i]);
            sema_pop_scope(s);
            break;
        }

        case AST_SYSCALL_DECL: {
            sema_push_scope(s);
            for (u32 i = 0; i < n->syscall_decl.param_count; i++) {
                sema_type(s, n->syscall_decl.params[i].type);
                if (n->syscall_decl.params[i].name.length > 0)
                    symtab_insert(s, n->syscall_decl.params[i].name, SYM_PARAM, n,
                        n->syscall_decl.params[i].type);
            }
            sema_type(s, n->syscall_decl.return_type);
            if (n->syscall_decl.body) sema_expr(s, n->syscall_decl.body);
            sema_pop_scope(s);
            break;
        }

        case AST_IMPL_BLOCK:
            sema_type(s, n->impl_block.target_type);
            sema_push_scope(s);
            for (u32 i = 0; i < n->impl_block.method_count; i++)
                sema_decl_pass2(s, n->impl_block.methods[i]);
            sema_pop_scope(s);
            break;

        case AST_TRAIT_DECL:
            break;

        default: break;
    }
}

/* ── Public API ───────────────────────────────────────────────── */

void sema_init(Sema *sema, Arena *arena, i32 source_id) {
    memset(sema, 0, sizeof(*sema));
    sema->arena     = arena;
    sema->source_id = source_id;
    /* Create global scope */
    SymTab *global = (SymTab *)arena_alloc(arena, sizeof(SymTab));
    memset(global, 0, sizeof(SymTab));
    sema->scope  = global;
    sema->global = global;
}

bool sema_analyze(Sema *sema, AstNode *program) {
    if (!program || program->kind != AST_PROGRAM) return false;

    /* Pass 1: register all top-level names */
    for (u32 i = 0; i < program->program.decl_count; i++)
        sema_decl_pass1(sema, program->program.decls[i]);

    /* Pass 2: analyze bodies */
    for (u32 i = 0; i < program->program.decl_count; i++)
        sema_decl_pass2(sema, program->program.decls[i]);

    return sema->error_count == 0;
}
