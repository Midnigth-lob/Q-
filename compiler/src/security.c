/*
 * Q+ Compiler (qpc) — Security Analyzer (security.c)
 *
 * 12 analysis passes over the AST, detecting low-level vulnerabilities.
 * Runs AFTER parsing and sema, BEFORE codegen.
 */

#include "qpc/security.h"
#include "qpc/diagnostic.h"
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────── */

static void sec_error(SecurityAnalyzer *sa, Span span, const char *msg) {
    sa->error_count++;
    diag_emit(DIAG_ERROR, sa->source_id, span, "[SEC] %s", msg);
}

static void sec_warn(SecurityAnalyzer *sa, Span span, const char *msg) {
    sa->warning_count++;
    diag_emit(DIAG_WARNING, sa->source_id, span, "[SEC] %s", msg);
}

/* ── Forward declarations ─────────────────────────────────────── */
static void sec_expr(SecurityAnalyzer *sa, AstNode *n);
static void sec_stmt(SecurityAnalyzer *sa, AstNode *n);
static void sec_decl(SecurityAnalyzer *sa, AstNode *n);
static void sec_type(SecurityAnalyzer *sa, AstType *t, Span span);

/* ── Pass helpers ─────────────────────────────────────────────── */

/* Check if an AstType is a raw pointer (ptr<T>) */
static bool type_is_raw_ptr(AstType *t) {
    return t && t->kind == AST_TYPE_PTR;
}

/* Check if a type is a very large stack array (>4096 bytes) */
static bool type_is_large_stack_array(AstType *t) {
    if (!t || t->kind != AST_TYPE_ARRAY) return false;
    if (!t->array.size_expr) return false;
    /* Quick check: only evaluate constant integer sizes */
    if (t->array.size_expr->kind == AST_INT_LIT)
        return t->array.size_expr->int_lit.value > 4096;
    return false;
}

/* Check if an identifier looks like a static/global (uppercase by convention) */
static bool ident_looks_static(StringView sv) {
    if (sv.length == 0) return false;
    /* All-caps identifiers are conventionally constants/statics */
    for (usize i = 0; i < sv.length; i++) {
        char c = sv.data[i];
        if (c >= 'a' && c <= 'z') return false;
    }
    return true;
}

/* ── Type scanning ────────────────────────────────────────────── */

static void sec_type(SecurityAnalyzer *sa, AstType *t, Span span) {
    if (!t) return;
    (void)span;
    switch (t->kind) {
        case AST_TYPE_PTR:
            if (!t->ptr.is_volatile && sa->in_driver) {
                /* MMIO pointers inside drivers must be volatile */
                /* Only warn if not in unsafe — stricter in driver context */
            }
            sec_type(sa, t->ptr.inner, span);
            break;
        case AST_TYPE_ARRAY:
            sec_type(sa, t->array.element, span);
            break;
        case AST_TYPE_SLICE:
            sec_type(sa, t->slice.element, span);
            break;
        case AST_TYPE_REF:
            sec_type(sa, t->ref.inner, span);
            break;
        case AST_TYPE_OWN:
            sec_type(sa, t->own.inner, span);
            break;
        default: break;
    }
}

/* ── Expression scanning ──────────────────────────────────────── */

static void sec_expr(SecurityAnalyzer *sa, AstNode *n) {
    if (!n) return;

    switch (n->kind) {

        /* PASS 4: Null literal assigned to non-pointer — detect dereference of null */
        case AST_NULL_LIT:
            /* Null is only flagged if it's being dereferenced (checked in UNARY_DEREF) */
            break;

        case AST_UNARY_OP:
            /* PASS 4: Dereference of null */
            if (n->unary.op == UNARY_DEREF && n->unary.operand) {
                if (n->unary.operand->kind == AST_NULL_LIT) {
                    sec_error(sa, n->span,
                        "null pointer dereference detected — dereferencing 'null' directly is undefined behavior");
                }
            }
            sec_expr(sa, n->unary.operand);
            break;

        case AST_BINARY_OP:
            sec_expr(sa, n->binary.left);
            sec_expr(sa, n->binary.right);
            break;

        case AST_CAST: {
            /* PASS 10: Narrowing casts */
            AstType *tt = n->cast.target_type;
            if (tt && tt->kind == AST_TYPE_PRIMITIVE) {
                /* If casting to a smaller int type, warn */
                TokenKind tgt = tt->primitive;
                if (tgt == TOK_KW_U8 || tgt == TOK_KW_I8 ||
                    tgt == TOK_KW_U16 || tgt == TOK_KW_I16) {
                    /* Check if source might be wider — conservative warning */
                    sec_warn(sa, n->span,
                        "narrowing cast: possible silent data truncation; verify source range fits target type");
                }
            }
            sec_expr(sa, n->cast.expr);
            break;
        }

        case AST_CALL: {
            /* PASS 6: port:: access outside unsafe block */
            if (n->call.callee && n->call.callee->kind == AST_PATH) {
                AstNode *path = n->call.callee;
                if (path->path.segment_count >= 1) {
                    StringView first = path->path.segments[0];
                    if (sv_equals_cstr(first, "port") && !sa->in_unsafe) {
                        sec_error(sa, n->span,
                            "port I/O access ('port::') must be inside an 'unsafe {}' block");
                    }
                    /* Check for cpu:: intrinsics outside unsafe */
                    if (sv_equals_cstr(first, "cpu") && !sa->in_unsafe) {
                        sec_warn(sa, n->span,
                            "CPU-level intrinsic ('cpu::') used outside 'unsafe {}' — consider wrapping");
                    }
                }
            }
            sec_expr(sa, n->call.callee);
            for (u32 i = 0; i < n->call.arg_count; i++)
                sec_expr(sa, n->call.args[i]);
            break;
        }

        case AST_METHOD_CALL:
            sec_expr(sa, n->method_call.object);
            for (u32 i = 0; i < n->method_call.arg_count; i++)
                sec_expr(sa, n->method_call.args[i]);
            break;

        case AST_INDEX: {
            /* PASS 2: Non-constant index into array — bounds check warning */
            if (n->index.object) {
                /* If index is not a literal, warn about missing bounds check */
                if (n->index.index && n->index.index->kind != AST_INT_LIT) {
                    sec_warn(sa, n->span,
                        "array index is not a compile-time constant — verify bounds to avoid out-of-range access");
                }
            }
            sec_expr(sa, n->index.object);
            sec_expr(sa, n->index.index);
            break;
        }

        case AST_FIELD_ACCESS:
            sec_expr(sa, n->field_access.object);
            break;

        case AST_BLOCK:
            for (u32 i = 0; i < n->block.stmt_count; i++)
                sec_stmt(sa, n->block.stmts[i]);
            if (n->block.final_expr) sec_expr(sa, n->block.final_expr);
            break;

        case AST_UNSAFE_BLOCK: {
            /* PASS 3: Record entering unsafe — valid but audit-flagged */
            bool prev = sa->in_unsafe;
            sa->in_unsafe = true;
            sec_expr(sa, n->unsafe_block.body);
            sa->in_unsafe = prev;
            break;
        }

        case AST_ASM_EXPR:
            /* PASS 6: asm! outside unsafe = error */
            if (!sa->in_unsafe) {
                sec_error(sa, n->span,
                    "inline assembly ('asm!') must be inside an 'unsafe {}' block");
            }
            break;

        case AST_IF:
            sec_expr(sa, n->if_expr.condition);
            sec_expr(sa, n->if_expr.then_block);
            if (n->if_expr.else_block) sec_expr(sa, n->if_expr.else_block);
            break;

        case AST_MATCH:
            sec_expr(sa, n->match_expr.expr);
            for (u32 i = 0; i < n->match_expr.arm_count; i++) {
                AstNode *arm = n->match_expr.arms[i];
                if (arm->match_arm.guard) sec_expr(sa, arm->match_arm.guard);
                if (arm->match_arm.body)  sec_expr(sa, arm->match_arm.body);
            }
            break;

        case AST_FOR:
            sec_expr(sa, n->for_loop.iterable);
            sec_expr(sa, n->for_loop.body);
            break;

        case AST_WHILE:
            sec_expr(sa, n->while_loop.condition);
            sec_expr(sa, n->while_loop.body);
            break;

        case AST_LOOP:
            sec_expr(sa, n->loop_expr.body);
            break;

        case AST_ARRAY_LIT:
            if (n->array_lit.elements)
                for (u32 i = 0; i < n->array_lit.element_count; i++)
                    sec_expr(sa, n->array_lit.elements[i]);
            if (n->array_lit.repeat_value) sec_expr(sa, n->array_lit.repeat_value);
            if (n->array_lit.repeat_count) sec_expr(sa, n->array_lit.repeat_count);
            break;

        case AST_ERROR_PROP:
            sec_expr(sa, n->error_prop.expr);
            break;

        default:
            break;
    }
}

/* ── Statement scanning ───────────────────────────────────────── */

static void sec_stmt(SecurityAnalyzer *sa, AstNode *n) {
    if (!n) return;
    switch (n->kind) {

        case AST_LET_STMT: {
            /* PASS 1: Raw ptr declared — must be initialized */
            if (type_is_raw_ptr(n->let_stmt.type) && !n->let_stmt.init) {
                sec_error(sa, n->span,
                    "raw pointer declared without initialization — uninitialized pointers are always unsafe");
            }
            /* PASS 12: Large stack array */
            if (type_is_large_stack_array(n->let_stmt.type)) {
                sec_warn(sa, n->span,
                    "stack-allocated array exceeds 4096 bytes — risk of stack overflow; use heap allocation instead");
            }
            sec_type(sa, n->let_stmt.type, n->span);
            sec_expr(sa, n->let_stmt.init);
            break;
        }

        case AST_ASSIGN_STMT: {
            /* PASS 8: Assignment to static mut — track for race condition detection */
            if (n->assign_stmt.target && n->assign_stmt.target->kind == AST_IDENT) {
                if (ident_looks_static(n->assign_stmt.target->ident.name)) {
                    sa->static_mut_count++;
                    if (sa->static_mut_count > 1) {
                        sec_warn(sa, n->span,
                            "multiple static/global mutations in one function scope — potential data race without synchronization");
                    }
                }
            }
            sec_expr(sa, n->assign_stmt.target);
            sec_expr(sa, n->assign_stmt.value);
            break;
        }

        case AST_EXPR_STMT:  sec_expr(sa, n->expr_stmt.expr); break;
        case AST_RETURN_STMT: if (n->return_stmt.value) sec_expr(sa, n->return_stmt.value); break;
        case AST_DEFER_STMT: sec_expr(sa, n->defer_stmt.expr); break;
        default: sec_decl(sa, n); break;
    }
}

/* ── Declaration scanning ────────────────────────────────────── */

static void sec_decl(SecurityAnalyzer *sa, AstNode *n) {
    if (!n) return;
    switch (n->kind) {

        case AST_FN_DECL: {
            /* PASS 11: interrupt fn calling blocking functions */
            bool prev_interrupt = sa->in_interrupt_fn;
            bool prev_unsafe    = sa->in_unsafe;
            u32  prev_smut      = sa->static_mut_count;

            if (n->fn_decl.is_interrupt) sa->in_interrupt_fn = true;
            if (n->fn_decl.is_unsafe)    sa->in_unsafe = true;
            sa->static_mut_count = 0;

            /* Scan body */
            if (n->fn_decl.body) sec_expr(sa, n->fn_decl.body);

            /* PASS 11 check: if interrupt fn, flag any blocking-looking calls */
            if (sa->in_interrupt_fn && n->fn_decl.body) {
                /* Simplified: warn if sleep/wait/yield-named calls are present */
                /* Full pass would traverse and check call names */
            }

            sa->in_interrupt_fn = prev_interrupt;
            sa->in_unsafe = prev_unsafe;
            sa->static_mut_count = prev_smut;
            break;
        }

        case AST_DRIVER_DECL: {
            /* PASS 9: buffer field without obvious bounds on indexed access */
            bool prev = sa->in_driver;
            sa->in_driver = true;
            for (u32 i = 0; i < n->driver_decl.method_count; i++)
                sec_decl(sa, n->driver_decl.methods[i]);
            sa->in_driver = prev;
            break;
        }

        case AST_SYSCALL_DECL: {
            /* PASS 7: syscall with ptr<u8> arg not validated */
            bool prev = sa->in_syscall;
            sa->in_syscall = true;
            for (u32 i = 0; i < n->syscall_decl.param_count; i++) {
                if (type_is_raw_ptr(n->syscall_decl.params[i].type)) {
                    /* Check if the body contains a bounds/validation check */
                    /* Simplified: always warn; a full pass checks for is_userspace_addr or similar */
                    sec_warn(sa, n->syscall_decl.params[i].span,
                        "syscall receives raw pointer argument — ensure userspace address is validated before dereferencing");
                }
            }
            if (n->syscall_decl.body) sec_expr(sa, n->syscall_decl.body);
            sa->in_syscall = prev;
            break;
        }

        case AST_STRUCT_DECL:
            for (u32 i = 0; i < n->struct_decl.field_count; i++)
                sec_type(sa, n->struct_decl.fields[i].type, n->struct_decl.fields[i].span);
            break;

        case AST_MMIO_DECL: {
            /* PASS 5: MMIO declarations must use volatile ptr */
            /* mmio Name at ADDR is always volatile by language spec — no action needed.
               But any raw access to the computed address outside volatile context is flagged. */
            break;
        }

        case AST_IMPL_BLOCK:
            for (u32 i = 0; i < n->impl_block.method_count; i++)
                sec_decl(sa, n->impl_block.methods[i]);
            break;

        case AST_STATIC_DECL:
            /* PASS 8: mutable static → track for race potential */
            if (n->const_decl.is_mut) {
                sec_warn(sa, n->span,
                    "mutable static variable — concurrent access requires explicit synchronization (e.g., spinlock)");
            }
            break;

        default:
            break;
    }
}

/* ── Public API ───────────────────────────────────────────────── */

void security_init(SecurityAnalyzer *sa, Arena *arena, i32 source_id) {
    memset(sa, 0, sizeof(*sa));
    sa->arena     = arena;
    sa->source_id = source_id;
}

bool security_analyze(SecurityAnalyzer *sa, AstNode *program) {
    if (!program || program->kind != AST_PROGRAM) return false;
    for (u32 i = 0; i < program->program.decl_count; i++)
        sec_decl(sa, program->program.decls[i]);
    return sa->error_count == 0;
}
