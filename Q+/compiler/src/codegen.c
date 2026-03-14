/*
 * Q+ Compiler (qpc) — C Code Generator (codegen.c)
 * Translates Q+ AST → freestanding C11.
 */

#include "qpc/codegen.h"
#include "qpc/diagnostic.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* ── CWriter ──────────────────────────────────────────────────── */

void cw_init(CWriter *cw) {
    cw->cap = 65536;
    cw->buf = (char *)malloc(cw->cap);
    cw->len = 0;
    cw->indent = 0;
    cw->buf[0] = '\0';
}

void cw_free(CWriter *cw) { free(cw->buf); cw->buf = NULL; }

static void cw_ensure(CWriter *cw, usize n) {
    if (cw->len + n + 1 >= cw->cap) {
        cw->cap = (cw->cap + n) * 2;
        cw->buf = (char *)realloc(cw->buf, cw->cap);
    }
}

void cw_write(CWriter *cw, const char *s) {
    usize n = strlen(s);
    cw_ensure(cw, n);
    memcpy(cw->buf + cw->len, s, n);
    cw->len += n;
    cw->buf[cw->len] = '\0';
}

void cw_writef(CWriter *cw, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    cw_write(cw, tmp);
}

void cw_nl(CWriter *cw) {
    cw_write(cw, "\n");
    for (int i = 0; i < cw->indent; i++) cw_write(cw, "    ");
}

void cw_writeln(CWriter *cw, const char *s) { cw_write(cw, s); cw_nl(cw); }
void cw_indent(CWriter *cw)  { cw->indent++; }
void cw_dedent(CWriter *cw)  { if (cw->indent > 0) cw->indent--; }

bool cw_flush_to_file(CWriter *cw, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fwrite(cw->buf, 1, cw->len, f);
    fclose(f);
    return true;
}

/* ── CodeGen context ──────────────────────────────────────────── */

void codegen_init(CodeGen *cg, Arena *arena) {
    cg->arena = arena;
    cw_init(&cg->out);
    cg->defer_top = 0;
}

void codegen_free(CodeGen *cg) { cw_free(&cg->out); }

/* ── Forward declarations ─────────────────────────────────────── */
static void cg_type(CodeGen *cg, AstType *t);
static void cg_expr(CodeGen *cg, AstNode *n);
static void cg_stmt(CodeGen *cg, AstNode *n);
static void cg_decl(CodeGen *cg, AstNode *n);
static void cg_block(CodeGen *cg, AstNode *n);

/* ── Type emission ────────────────────────────────────────────── */

static void cg_primitive(CodeGen *cg, TokenKind k) {
    CWriter *o = &cg->out;
    switch (k) {
        case TOK_KW_U8:    cw_write(o, "uint8_t");   break;
        case TOK_KW_U16:   cw_write(o, "uint16_t");  break;
        case TOK_KW_U32:   cw_write(o, "uint32_t");  break;
        case TOK_KW_U64:   cw_write(o, "uint64_t");  break;
        case TOK_KW_I8:    cw_write(o, "int8_t");    break;
        case TOK_KW_I16:   cw_write(o, "int16_t");   break;
        case TOK_KW_I32:   cw_write(o, "int32_t");   break;
        case TOK_KW_I64:   cw_write(o, "int64_t");   break;
        case TOK_KW_F32:   cw_write(o, "float");     break;
        case TOK_KW_F64:   cw_write(o, "double");    break;
        case TOK_KW_BOOL:  cw_write(o, "bool");      break;
        case TOK_KW_CHAR:  cw_write(o, "uint32_t");  break;  /* Unicode codepoint */
        case TOK_KW_STR:   cw_write(o, "const char*"); break;
        case TOK_KW_VOID:  cw_write(o, "void");      break;
        case TOK_KW_USIZE: cw_write(o, "size_t");    break;
        case TOK_KW_ISIZE: cw_write(o, "ptrdiff_t"); break;
        default:           cw_write(o, "int");        break;
    }
}

static void cg_type(CodeGen *cg, AstType *t) {
    CWriter *o = &cg->out;
    if (!t) { cw_write(o, "void"); return; }
    switch (t->kind) {
        case AST_TYPE_PRIMITIVE:
            cg_primitive(cg, t->primitive);
            break;
        case AST_TYPE_NAMED:
            cw_writef(o, "%.*s", (int)t->named.name.length, t->named.name.data);
            break;
        case AST_TYPE_PATH:
            if (t->named.path) cg_expr(cg, t->named.path);
            else cw_writef(o, "%.*s", (int)t->named.name.length, t->named.name.data);
            break;
        case AST_TYPE_PTR:
            cg_type(cg, t->ptr.inner);
            if (t->ptr.is_volatile) cw_write(o, " volatile");
            cw_write(o, "*");
            break;
        case AST_TYPE_REF:
            cg_type(cg, t->ref.inner);
            cw_write(o, "*");
            break;
        case AST_TYPE_OWN:
            cg_type(cg, t->own.inner);
            cw_write(o, "*");
            break;
        case AST_TYPE_SLICE:
            /* slice<T> → qp_slice (a fat pointer struct) */
            cw_write(o, "qp_slice");
            break;
        case AST_TYPE_ARRAY:
            /* Array types: caller appends [N] after var name; emit element type here */
            cg_type(cg, t->array.element);
            break;
        case AST_TYPE_GENERIC:
            /* Generic instantiation emitted as base name */
            cg_type(cg, t->generic.base);
            break;
        case AST_TYPE_NEVER:
            cw_write(o, "void __attribute__((noreturn))");
            break;
        case AST_TYPE_FN_PTR:
            cg_type(cg, t->fn_ptr.return_type);
            cw_write(o, "(*)(");
            for (u32 i = 0; i < t->fn_ptr.param_count; i++) {
                if (i) cw_write(o, ", ");
                cg_type(cg, t->fn_ptr.params[i]);
            }
            cw_write(o, ")");
            break;
        case AST_TYPE_INFERRED:
        default:
            cw_write(o, "__auto_type");
            break;
    }
}

/* Emit array suffix [N] if type is array */
static void cg_type_array_suffix(CodeGen *cg, AstType *t) {
    if (!t || t->kind != AST_TYPE_ARRAY) return;
    CWriter *o = &cg->out;
    cw_write(o, "[");
    if (t->array.size_expr) cg_expr(cg, t->array.size_expr);
    cw_write(o, "]");
}

/* ── Binary operator string ───────────────────────────────────── */

static const char *binop_str(BinaryOp op) {
    switch (op) {
        case BIN_ADD: return "+";  case BIN_SUB: return "-";
        case BIN_MUL: return "*";  case BIN_DIV: return "/";
        case BIN_MOD: return "%";
        case BIN_BIT_AND: return "&"; case BIN_BIT_OR: return "|";
        case BIN_BIT_XOR: return "^";
        case BIN_SHL: return "<<"; case BIN_SHR: return ">>";
        case BIN_EQ: return "==";  case BIN_NEQ: return "!=";
        case BIN_LT: return "<";   case BIN_GT: return ">";
        case BIN_LT_EQ: return "<="; case BIN_GT_EQ: return ">=";
        case BIN_AND: return "&&"; case BIN_OR: return "||";
        default: return "+";
    }
}

static const char *assignop_str(AssignOp op) {
    switch (op) {
        case ASSIGN_EQ:      return "=";
        case ASSIGN_ADD:     return "+=";  case ASSIGN_SUB: return "-=";
        case ASSIGN_MUL:     return "*=";  case ASSIGN_DIV: return "/=";
        case ASSIGN_MOD:     return "%=";
        case ASSIGN_BIT_AND: return "&=";  case ASSIGN_BIT_OR: return "|=";
        case ASSIGN_BIT_XOR: return "^=";
        case ASSIGN_SHL:     return "<<="; case ASSIGN_SHR: return ">>=";
        default: return "=";
    }
}

/* ── Expression emission ──────────────────────────────────────── */

static void cg_expr(CodeGen *cg, AstNode *n) {
    CWriter *o = &cg->out;
    if (!n) return;

    switch (n->kind) {

        case AST_INT_LIT:
            if (n->int_lit.base == INT_BASE_HEX)
                cw_writef(o, "0x%llX", (unsigned long long)n->int_lit.value);
            else if (n->int_lit.base == INT_BASE_BIN)
                cw_writef(o, "%lluULL /* 0b */", (unsigned long long)n->int_lit.value);
            else if (n->int_lit.base == INT_BASE_OCT)
                cw_writef(o, "0%llo", (unsigned long long)n->int_lit.value);
            else
                cw_writef(o, "%llu", (unsigned long long)n->int_lit.value);
            break;

        case AST_FLOAT_LIT:
            cw_writef(o, "%g", n->float_lit.value);
            break;

        case AST_STRING_LIT:
            cw_write(o, "\"");
            cw_writef(o, "%.*s", (int)n->string_lit.value.length, n->string_lit.value.data);
            cw_write(o, "\"");
            break;

        case AST_CHAR_LIT:
            cw_writef(o, "'%c'", (char)n->char_lit.value);
            break;

        case AST_BOOL_LIT:
            cw_write(o, n->bool_lit.value ? "true" : "false");
            break;

        case AST_NULL_LIT:
            cw_write(o, "NULL");
            break;

        case AST_IDENT:
            cw_writef(o, "%.*s", (int)n->ident.name.length, n->ident.name.data);
            break;

        case AST_PATH:
            for (u32 i = 0; i < n->path.segment_count; i++) {
                if (i) cw_write(o, "_");
                cw_writef(o, "%.*s", (int)n->path.segments[i].length, n->path.segments[i].data);
            }
            break;

        case AST_BINARY_OP:
            if (n->binary.op == BIN_RANGE || n->binary.op == BIN_RANGE_INCLUSIVE) {
                /* Ranges not directly expressible in C; emit start only as fallback */
                cg_expr(cg, n->binary.left);
            } else {
                cw_write(o, "(");
                cg_expr(cg, n->binary.left);
                cw_writef(o, " %s ", binop_str(n->binary.op));
                cg_expr(cg, n->binary.right);
                cw_write(o, ")");
            }
            break;

        case AST_UNARY_OP:
            switch (n->unary.op) {
                case UNARY_NEG:     cw_write(o, "-("); cg_expr(cg, n->unary.operand); cw_write(o, ")"); break;
                case UNARY_BIT_NOT: cw_write(o, "~("); cg_expr(cg, n->unary.operand); cw_write(o, ")"); break;
                case UNARY_LOG_NOT: cw_write(o, "!("); cg_expr(cg, n->unary.operand); cw_write(o, ")"); break;
                case UNARY_DEREF:   cw_write(o, "*("); cg_expr(cg, n->unary.operand); cw_write(o, ")"); break;
                case UNARY_REF:
                case UNARY_REF_MUT: cw_write(o, "&("); cg_expr(cg, n->unary.operand); cw_write(o, ")"); break;
                default: cg_expr(cg, n->unary.operand); break;
            }
            break;

        case AST_CALL:
            cg_expr(cg, n->call.callee);
            cw_write(o, "(");
            for (u32 i = 0; i < n->call.arg_count; i++) {
                if (i) cw_write(o, ", ");
                cg_expr(cg, n->call.args[i]);
            }
            cw_write(o, ")");
            break;

        case AST_METHOD_CALL:
            /* obj.method(args) — emit as method_TypeOf(obj, args) in C */
            cg_expr(cg, n->method_call.object);
            cw_writef(o, "_%.*s_(", (int)n->method_call.method_name.length, n->method_call.method_name.data);
            cw_write(o, "&(");
            cg_expr(cg, n->method_call.object);
            cw_write(o, ")");
            for (u32 i = 0; i < n->method_call.arg_count; i++) {
                cw_write(o, ", ");
                cg_expr(cg, n->method_call.args[i]);
            }
            cw_write(o, ")");
            break;

        case AST_FIELD_ACCESS:
            cg_expr(cg, n->field_access.object);
            cw_writef(o, ".%.*s", (int)n->field_access.field_name.length, n->field_access.field_name.data);
            break;

        case AST_INDEX:
            cg_expr(cg, n->index.object);
            cw_write(o, "[");
            cg_expr(cg, n->index.index);
            cw_write(o, "]");
            break;

        case AST_CAST:
            cw_write(o, "((");
            cg_type(cg, n->cast.target_type);
            cw_write(o, ")(");
            cg_expr(cg, n->cast.expr);
            cw_write(o, "))");
            break;

        case AST_BLOCK:
            cg_block(cg, n);
            break;

        case AST_IF:
            cw_write(o, "if (");
            cg_expr(cg, n->if_expr.condition);
            cw_write(o, ") ");
            cg_block(cg, n->if_expr.then_block);
            if (n->if_expr.else_block) {
                cw_write(o, " else ");
                if (n->if_expr.else_block->kind == AST_IF)
                    cg_expr(cg, n->if_expr.else_block);
                else
                    cg_block(cg, n->if_expr.else_block);
            }
            break;

        case AST_MATCH: {
            /* Emit as if-else chain — C has no match */
            cw_write(o, "{ __auto_type _match_val_ = (");
            cg_expr(cg, n->match_expr.expr);
            cw_write(o, ");");
            cw_indent(o);
            for (u32 i = 0; i < n->match_expr.arm_count; i++) {
                AstNode *arm = n->match_expr.arms[i];
                cw_nl(o);
                if (i == 0) cw_write(o, "if (");
                else        cw_write(o, "else if (");
                /* Pattern matching: emit wildcard (_) as else */
                if (arm->match_arm.pattern &&
                    arm->match_arm.pattern->kind == AST_IDENT) {
                    StringView pname = arm->match_arm.pattern->ident.name;
                    if (pname.length == 1 && pname.data[0] == '_') {
                        /* Wildcard: close condition with true */
                        cw_write(o, "1");
                    } else {
                        cw_write(o, "_match_val_ == ");
                        cg_expr(cg, arm->match_arm.pattern);
                    }
                } else if (arm->match_arm.pattern) {
                    cw_write(o, "_match_val_ == ");
                    cg_expr(cg, arm->match_arm.pattern);
                } else {
                    cw_write(o, "1");
                }
                cw_write(o, ") ");
                if (arm->match_arm.body)
                    cg_block(cg, arm->match_arm.body);
            }
            cw_dedent(o);
            cw_nl(o);
            cw_write(o, "}");
            break;
        }

        case AST_FOR:
            /* for x in 0..n  → for(size_t x=start; x<end; x++) */
            cw_write(o, "for (size_t ");
            cw_writef(o, "%.*s", (int)n->for_loop.var_name.length, n->for_loop.var_name.data);
            cw_write(o, " = 0; (size_t)");
            cw_writef(o, "%.*s", (int)n->for_loop.var_name.length, n->for_loop.var_name.data);
            cw_write(o, " < (size_t)(");
            cg_expr(cg, n->for_loop.iterable);
            cw_write(o, "); ");
            cw_writef(o, "%.*s", (int)n->for_loop.var_name.length, n->for_loop.var_name.data);
            cw_write(o, "++) ");
            cg_block(cg, n->for_loop.body);
            break;

        case AST_WHILE:
            cw_write(o, "while (");
            cg_expr(cg, n->while_loop.condition);
            cw_write(o, ") ");
            cg_block(cg, n->while_loop.body);
            break;

        case AST_LOOP:
            cw_write(o, "for (;;) ");
            cg_block(cg, n->loop_expr.body);
            break;

        case AST_ARRAY_LIT:
            cw_write(o, "{");
            if (n->array_lit.elements) {
                for (u32 i = 0; i < n->array_lit.element_count; i++) {
                    if (i) cw_write(o, ", ");
                    cg_expr(cg, n->array_lit.elements[i]);
                }
            } else if (n->array_lit.repeat_value) {
                /* [val; N] — not directly portable; emit initializer */
                cg_expr(cg, n->array_lit.repeat_value);
            }
            cw_write(o, "}");
            break;

        case AST_UNSAFE_BLOCK:
            /* Unsafe blocks in Q+ are just regular blocks in C */
            cg_block(cg, n->unsafe_block.body);
            break;

        case AST_ASM_EXPR:
            /* asm!("template") → __asm__ volatile ("template"); */
            cw_write(o, "__asm__ volatile (\"");
            cw_writef(o, "%.*s", (int)n->asm_expr.template_str.length, n->asm_expr.template_str.data);
            cw_write(o, "\")");
            break;

        case AST_ERROR_PROP:
            /* expr? — emit as expression; actual propagation needs return-type awareness */
            cg_expr(cg, n->error_prop.expr);
            break;

        case AST_STRUCT_LIT:
            cw_write(o, "(");
            cg_type(cg, n->struct_lit.type_name);
            cw_write(o, "){");
            for (u32 i = 0; i < n->struct_lit.field_count; i++) {
                if (i) cw_write(o, ", ");
                cw_writef(o, ".%.*s = ", (int)n->struct_lit.field_names[i].length, n->struct_lit.field_names[i].data);
                cg_expr(cg, n->struct_lit.field_values[i]);
            }
            cw_write(o, "}");
            break;

        default:
            cw_write(o, "/* <unsupported-expr> */");
            break;
    }
}

/* ── Block emission ───────────────────────────────────────────── */

static void cg_block(CodeGen *cg, AstNode *n) {
    CWriter *o = &cg->out;
    if (!n) { cw_write(o, "{}"); return; }
    if (n->kind != AST_BLOCK) { cg_expr(cg, n); return; }

    u32 defer_mark = cg->defer_top;

    cw_write(o, "{");
    cw_indent(o);

    for (u32 i = 0; i < n->block.stmt_count; i++) {
        cw_nl(o);
        cg_stmt(cg, n->block.stmts[i]);
    }

    /* Emit deferred expressions in LIFO order before block ends */
    for (u32 i = cg->defer_top; i > defer_mark; i--) {
        cw_nl(o);
        cg_expr(cg, cg->defer_stack[i - 1]);
        cw_write(o, ";");
    }
    cg->defer_top = defer_mark;

    if (n->block.final_expr) {
        cw_nl(o);
        cg_expr(cg, n->block.final_expr);
        cw_write(o, ";");
    }
    cw_dedent(o);
    cw_nl(o);
    cw_write(o, "}");
}

/* ── Statement emission ───────────────────────────────────────── */

static void cg_stmt(CodeGen *cg, AstNode *n) {
    CWriter *o = &cg->out;
    if (!n) return;
    switch (n->kind) {

        case AST_LET_STMT:
            if (n->let_stmt.type) {
                cg_type(cg, n->let_stmt.type);
            } else {
                cw_write(o, "__auto_type");
            }
            cw_write(o, " ");
            cw_writef(o, "%.*s", (int)n->let_stmt.name.length, n->let_stmt.name.data);
            cg_type_array_suffix(cg, n->let_stmt.type);
            if (n->let_stmt.init) {
                cw_write(o, " = ");
                cg_expr(cg, n->let_stmt.init);
            }
            cw_write(o, ";");
            break;

        case AST_ASSIGN_STMT:
            cg_expr(cg, n->assign_stmt.target);
            cw_writef(o, " %s ", assignop_str(n->assign_stmt.op));
            cg_expr(cg, n->assign_stmt.value);
            cw_write(o, ";");
            break;

        case AST_RETURN_STMT:
            cw_write(o, "return");
            if (n->return_stmt.value) {
                cw_write(o, " ");
                cg_expr(cg, n->return_stmt.value);
            }
            cw_write(o, ";");
            break;

        case AST_BREAK_STMT:
            cw_write(o, "break;");
            break;

        case AST_CONTINUE_STMT:
            cw_write(o, "continue;");
            break;

        case AST_DEFER_STMT:
            /* Push onto defer stack; will be emitted at block end */
            if (cg->defer_top < 256)
                cg->defer_stack[cg->defer_top++] = n->defer_stmt.expr;
            break;

        case AST_EXPR_STMT:
            cg_expr(cg, n->expr_stmt.expr);
            cw_write(o, ";");
            break;

        default:
            /* Declarations that appear inside a function body */
            cg_decl(cg, n);
            break;
    }
}

/* ── Attributes ───────────────────────────────────────────────── */

static void cg_attribs(CodeGen *cg, AstAttribute *attribs, u32 count) {
    CWriter *o = &cg->out;
    for (u32 i = 0; i < count; i++) {
        StringView name = attribs[i].name;
        if (sv_equals_cstr(name, "no_mangle")) {
            /* Don't mangle the symbol (already applies in C) */
        } else if (sv_equals_cstr(name, "packed")) {
            cw_write(o, " __attribute__((packed))");
        } else if (sv_equals_cstr(name, "align")) {
            cw_write(o, " __attribute__((aligned(16)))");
        } else if (sv_equals_cstr(name, "link_section") || sv_equals_cstr(name, "section")) {
            /* emitted before the function signature */
        } else if (sv_equals_cstr(name, "irq_handler")) {
            cw_write(o, " __attribute__((interrupt))");
        }
    }
}

static void cg_section_attrib(CodeGen *cg, AstAttribute *attribs, u32 count) {
    CWriter *o = &cg->out;
    for (u32 i = 0; i < count; i++) {
        StringView name = attribs[i].name;
        if (sv_equals_cstr(name, "link_section") || sv_equals_cstr(name, "section")) {
            cw_write(o, "__attribute__((section(\".text.boot\"))) ");
        }
    }
}

/* ── Declaration emission ─────────────────────────────────────── */

static void cg_fn_decl(CodeGen *cg, AstNode *n, const char *prefix) {
    CWriter *o = &cg->out;
    /* Return type */
    if (n->fn_decl.is_interrupt) {
        cw_write(o, "__attribute__((interrupt)) void");
    } else if (n->fn_decl.return_type &&
               n->fn_decl.return_type->kind == AST_TYPE_NEVER) {
        cw_write(o, "__attribute__((noreturn)) void");
    } else {
        cg_type(cg, n->fn_decl.return_type);
    }
    cw_write(o, " ");
    /* Section attribute before name */
    cg_section_attrib(cg, n->fn_decl.attribs, n->fn_decl.attrib_count);
    /* Name */
    if (prefix) {
        cw_writef(o, "%s_", prefix);
    }
    cw_writef(o, "%.*s", (int)n->fn_decl.name.length, n->fn_decl.name.data);
    /* Params */
    cw_write(o, "(");
    for (u32 i = 0; i < n->fn_decl.param_count; i++) {
        if (i) cw_write(o, ", ");
        AstParam *p = &n->fn_decl.params[i];
        if (p->type) {
            cg_type(cg, p->type);
        } else {
            /* self parameter */
            if (prefix) cw_writef(o, "%s*", prefix);
            else cw_write(o, "void*");
        }
        cw_write(o, " ");
        cw_writef(o, "%.*s", (int)p->name.length, p->name.data);
    }
    if (n->fn_decl.param_count == 0) cw_write(o, "void");
    cw_write(o, ")");
    /* Trailing packed/interrupt attribs */
    cg_attribs(cg, n->fn_decl.attribs, n->fn_decl.attrib_count);
    if (n->fn_decl.body) {
        cw_write(o, " ");
        cg_block(cg, n->fn_decl.body);
    } else {
        cw_write(o, ";");
    }
}

static void cg_decl(CodeGen *cg, AstNode *n) {
    CWriter *o = &cg->out;
    if (!n) return;

    switch (n->kind) {

        case AST_MODULE_DECL:
            cw_write(o, "/* Q+ module: ");
            if (n->module_decl.path) cg_expr(cg, n->module_decl.path);
            cw_write(o, " */");
            cw_nl(o);
            break;

        case AST_IMPORT_DECL:
            cw_write(o, "/* import: ");
            if (n->import_decl.path) cg_expr(cg, n->import_decl.path);
            cw_write(o, " */");
            cw_nl(o);
            break;

        case AST_CONST_DECL:
            cw_write(o, "#define ");
            cw_writef(o, "%.*s", (int)n->const_decl.name.length, n->const_decl.name.data);
            cw_write(o, " (");
            cg_expr(cg, n->const_decl.value);
            cw_write(o, ")");
            cw_nl(o);
            break;

        case AST_STATIC_DECL:
            cw_write(o, "static ");
            cg_type(cg, n->const_decl.type);
            cw_write(o, " ");
            cw_writef(o, "%.*s", (int)n->const_decl.name.length, n->const_decl.name.data);
            cg_type_array_suffix(cg, n->const_decl.type);
            if (n->const_decl.value) {
                cw_write(o, " = ");
                cg_expr(cg, n->const_decl.value);
            }
            cw_write(o, ";");
            cw_nl(o);
            break;

        case AST_TYPE_ALIAS:
            cw_write(o, "typedef ");
            cg_type(cg, n->type_alias.target);
            cw_write(o, " ");
            cw_writef(o, "%.*s", (int)n->type_alias.name.length, n->type_alias.name.data);
            cw_write(o, ";");
            cw_nl(o);
            break;

        case AST_STRUCT_DECL: {
            /* Forward declaration */
            cw_writef(o, "typedef struct %.*s %.*s;",
                (int)n->struct_decl.name.length, n->struct_decl.name.data,
                (int)n->struct_decl.name.length, n->struct_decl.name.data);
            cw_nl(o);
            /* Check packed attribute */
            bool packed = false;
            for (u32 i = 0; i < n->struct_decl.attrib_count; i++)
                if (sv_equals_cstr(n->struct_decl.attribs[i].name, "packed")) packed = true;
            cw_writef(o, "struct %.*s {",
                (int)n->struct_decl.name.length, n->struct_decl.name.data);
            cw_indent(o);
            for (u32 i = 0; i < n->struct_decl.field_count; i++) {
                cw_nl(o);
                cg_type(cg, n->struct_decl.fields[i].type);
                cw_write(o, " ");
                cw_writef(o, "%.*s", (int)n->struct_decl.fields[i].name.length,
                    n->struct_decl.fields[i].name.data);
                cg_type_array_suffix(cg, n->struct_decl.fields[i].type);
                cw_write(o, ";");
            }
            cw_dedent(o);
            cw_nl(o);
            if (packed) cw_write(o, "} __attribute__((packed));");
            else        cw_write(o, "};");
            cw_nl(o);
            break;
        }

        case AST_ENUM_DECL: {
            /* Repr type */
            cw_write(o, "typedef ");
            if (n->enum_decl.repr_type) cg_type(cg, n->enum_decl.repr_type);
            else cw_write(o, "int");
            cw_write(o, " ");
            cw_writef(o, "%.*s;", (int)n->enum_decl.name.length, n->enum_decl.name.data);
            cw_nl(o);
            /* Constants for each variant */
            for (u32 i = 0; i < n->enum_decl.variant_count; i++) {
                AstVariant *v = &n->enum_decl.variants[i];
                cw_write(o, "#define ");
                cw_writef(o, "%.*s_%.*s ",
                    (int)n->enum_decl.name.length, n->enum_decl.name.data,
                    (int)v->name.length, v->name.data);
                if (v->discriminant) { cw_write(o, "("); cg_expr(cg, v->discriminant); cw_write(o, ")"); }
                else cw_writef(o, "(%u)", i);
                cw_nl(o);
            }
            break;
        }

        case AST_FN_DECL:
            cg_fn_decl(cg, n, NULL);
            cw_nl(o);
            break;

        case AST_DRIVER_DECL: {
            /* Emit as typedef struct + forward-named functions */
            char dname[256];
            snprintf(dname, sizeof(dname), "%.*s",
                (int)n->driver_decl.name.length, n->driver_decl.name.data);
            cw_writef(o, "typedef struct %s %s;", dname, dname);
            cw_nl(o);
            cw_writef(o, "struct %s {", dname);
            cw_indent(o);
            for (u32 i = 0; i < n->driver_decl.field_count; i++) {
                cw_nl(o);
                cg_type(cg, n->driver_decl.fields[i].type);
                cw_write(o, " ");
                cw_writef(o, "%.*s", (int)n->driver_decl.fields[i].name.length,
                    n->driver_decl.fields[i].name.data);
                cg_type_array_suffix(cg, n->driver_decl.fields[i].type);
                cw_write(o, ";");
            }
            cw_dedent(o);
            cw_nl(o);
            cw_write(o, "};");
            cw_nl(o);
            /* Methods: emit as DriverName_methodname(DriverName *self, ...) */
            for (u32 i = 0; i < n->driver_decl.method_count; i++) {
                cw_nl(o);
                cg_fn_decl(cg, n->driver_decl.methods[i], dname);
                cw_nl(o);
            }
            break;
        }

        case AST_SYSCALL_DECL:
            /* Syscalls: plain C function, tagged with a comment */
            cw_write(o, "/* syscall */ ");
            cg_type(cg, n->syscall_decl.return_type);
            cw_write(o, " sys_");
            cw_writef(o, "%.*s", (int)n->syscall_decl.name.length, n->syscall_decl.name.data);
            cw_write(o, "(");
            for (u32 i = 0; i < n->syscall_decl.param_count; i++) {
                if (i) cw_write(o, ", ");
                cg_type(cg, n->syscall_decl.params[i].type);
                cw_write(o, " ");
                cw_writef(o, "%.*s", (int)n->syscall_decl.params[i].name.length,
                    n->syscall_decl.params[i].name.data);
            }
            if (n->syscall_decl.param_count == 0) cw_write(o, "void");
            cw_write(o, ") ");
            if (n->syscall_decl.body) cg_block(cg, n->syscall_decl.body);
            else cw_write(o, "{}");
            cw_nl(o);
            break;

        case AST_IMPL_BLOCK:
            /* impl blocks: emit each method */
            for (u32 i = 0; i < n->impl_block.method_count; i++) {
                cw_nl(o);
                cg_decl(cg, n->impl_block.methods[i]);
            }
            break;

        case AST_TRAIT_DECL:
            /* Traits: emit method signatures as function pointer typedefs */
            cw_writef(o, "/* trait %.*s */",
                (int)n->trait_decl.name.length, n->trait_decl.name.data);
            cw_nl(o);
            break;

        default:
            /* Statement-level declarations */
            cg_stmt(cg, n);
            break;
    }
}

/* ── Preamble ─────────────────────────────────────────────────── */

static void cg_preamble(CodeGen *cg) {
    CWriter *o = &cg->out;
    cw_writeln(o, "/*");
    cw_writeln(o, " * Generated by qpc (Q+ Compiler) — DO NOT EDIT");
    cw_writeln(o, " * Freestanding C11 — no libc required");
    cw_writeln(o, " */");
    cw_nl(o);
    cw_writeln(o, "#include <stdint.h>");
    cw_writeln(o, "#include <stddef.h>");
    cw_writeln(o, "#include <stdbool.h>");
    cw_nl(o);
    /* Q+ runtime helpers */
    cw_writeln(o, "/* Q+ fat-pointer slice type */");
    cw_writeln(o, "typedef struct { void *ptr; size_t len; } qp_slice;");
    cw_nl(o);
    /* Port I/O helpers */
    cw_writeln(o, "/* Port I/O (x86) */");
    cw_writeln(o, "static inline void qp_port_out_u8(uint16_t port, uint8_t val) {");
    cw_writeln(o, "    __asm__ volatile (\"outb %0, %1\" : : \"a\"(val), \"Nd\"(port));");
    cw_writeln(o, "}");
    cw_writeln(o, "static inline uint8_t qp_port_in_u8(uint16_t port) {");
    cw_writeln(o, "    uint8_t val;");
    cw_writeln(o, "    __asm__ volatile (\"inb %1, %0\" : \"=a\"(val) : \"Nd\"(port));");
    cw_writeln(o, "    return val;");
    cw_writeln(o, "}");
    cw_writeln(o, "static inline void qp_port_out_u16(uint16_t port, uint16_t val) {");
    cw_writeln(o, "    __asm__ volatile (\"outw %0, %1\" : : \"a\"(val), \"Nd\"(port));");
    cw_writeln(o, "}");
    cw_writeln(o, "static inline uint16_t qp_port_in_u16(uint16_t port) {");
    cw_writeln(o, "    uint16_t val;");
    cw_writeln(o, "    __asm__ volatile (\"inw %1, %0\" : \"=a\"(val) : \"Nd\"(port));");
    cw_writeln(o, "    return val;");
    cw_writeln(o, "}");
    cw_writeln(o, "static inline void qp_port_out_u32(uint16_t port, uint32_t val) {");
    cw_writeln(o, "    __asm__ volatile (\"outl %0, %1\" : : \"a\"(val), \"Nd\"(port));");
    cw_writeln(o, "}");
    cw_writeln(o, "static inline uint32_t qp_port_in_u32(uint16_t port) {");
    cw_writeln(o, "    uint32_t val;");
    cw_writeln(o, "    __asm__ volatile (\"inl %1, %0\" : \"=a\"(val) : \"Nd\"(port));");
    cw_writeln(o, "    return val;");
    cw_writeln(o, "}");
    cw_nl(o);
    /* MMIO macro */
    cw_writeln(o, "/* MMIO access helpers */");
    cw_writeln(o, "#define QP_MMIO_READ8(addr)        (*(volatile uint8_t  *)(uintptr_t)(addr))");
    cw_writeln(o, "#define QP_MMIO_READ16(addr)       (*(volatile uint16_t *)(uintptr_t)(addr))");
    cw_writeln(o, "#define QP_MMIO_READ32(addr)       (*(volatile uint32_t *)(uintptr_t)(addr))");
    cw_writeln(o, "#define QP_MMIO_READ64(addr)       (*(volatile uint64_t *)(uintptr_t)(addr))");
    cw_writeln(o, "#define QP_MMIO_WRITE8(addr, val)  (*(volatile uint8_t  *)(uintptr_t)(addr) = (val))");
    cw_writeln(o, "#define QP_MMIO_WRITE16(addr, val) (*(volatile uint16_t *)(uintptr_t)(addr) = (val))");
    cw_writeln(o, "#define QP_MMIO_WRITE32(addr, val) (*(volatile uint32_t *)(uintptr_t)(addr) = (val))");
    cw_writeln(o, "#define QP_MMIO_WRITE64(addr, val) (*(volatile uint64_t *)(uintptr_t)(addr) = (val))");
    cw_nl(o);
}

/* ── Main entry point ─────────────────────────────────────────── */

bool codegen_emit_c(AstNode *program, const char *out_path) {
    if (!program || program->kind != AST_PROGRAM) return false;

    Arena arena;
    arena_init(&arena);

    CodeGen cg;
    codegen_init(&cg, &arena);

    cg_preamble(&cg);

    for (u32 i = 0; i < program->program.decl_count; i++) {
        cw_nl(&cg.out);
        cg_decl(&cg, program->program.decls[i]);
    }

    bool ok = cw_flush_to_file(&cg.out, out_path);
    codegen_free(&cg);
    arena_free(&arena);
    return ok;
}
