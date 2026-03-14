/*
 * Q+ Compiler (qpc) — Parser (Implementation)
 * Recursive descent parser: tokens → AST
 */

#include "qpc/parser.h"
#include "qpc/diagnostic.h"

/* ── Token helpers ───────────────────────────────────────────── */

static void advance(Parser *p) {
    p->previous = p->current;
    p->current = lexer_next_token(p->lexer);
}

static bool check(Parser *p, TokenKind kind) {
    return p->current.kind == kind;
}

static bool match(Parser *p, TokenKind kind) {
    if (!check(p, kind)) return false;
    advance(p);
    return true;
}

static void error_at(Parser *p, Span span, const char *msg) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;
    diag_emit(DIAG_ERROR, p->source_id, span, "%s", msg);
}

static void error_current(Parser *p, const char *msg) {
    error_at(p, p->current.span, msg);
}

static bool expect(Parser *p, TokenKind kind, const char *msg) {
    if (check(p, kind)) { advance(p); return true; }
    error_current(p, msg);
    return false;
}

static void synchronize(Parser *p) {
    p->panic_mode = false;
    while (p->current.kind != TOK_EOF) {
        if (p->previous.kind == TOK_SEMICOLON) return;
        switch (p->current.kind) {
            case TOK_KW_FN: case TOK_KW_LET: case TOK_KW_STRUCT:
            case TOK_KW_ENUM: case TOK_KW_IMPL: case TOK_KW_TRAIT:
            case TOK_KW_PUB: case TOK_KW_IMPORT: case TOK_KW_MODULE:
            case TOK_KW_CONST: case TOK_KW_STATIC: case TOK_KW_DRIVER:
            case TOK_KW_SYSCALL: case TOK_KW_RETURN: case TOK_KW_IF:
            case TOK_KW_FOR: case TOK_KW_WHILE: case TOK_KW_LOOP:
                return;
            default: advance(p); break;
        }
    }
}

/* ── Forward declarations ────────────────────────────────────── */
static AstNode *parse_expression(Parser *p);
static AstNode *parse_statement(Parser *p);
static AstNode *parse_block(Parser *p);
static AstType *parse_type(Parser *p);
static AstNode *parse_declaration(Parser *p);

/* ── Type parsing ────────────────────────────────────────────── */

static bool is_primitive_type(TokenKind k) {
    return k >= TOK_KW_U8 && k <= TOK_KW_ISIZE;
}

static AstType *parse_type(Parser *p) {
    Span start = p->current.span;

    /* Never type: ! */
    if (match(p, TOK_BANG)) {
        return ast_type_new(p->arena, AST_TYPE_NEVER, start);
    }

    /* Reference: &T or &mut T */
    if (match(p, TOK_AMP)) {
        bool is_mut = match(p, TOK_KW_MUT);
        AstType *inner = parse_type(p);
        AstType *t = ast_type_new(p->arena, AST_TYPE_REF, start);
        t->ref.inner = inner;
        t->ref.is_mut = is_mut;
        return t;
    }

    /* ptr<T> */
    if (match(p, TOK_KW_PTR)) {
        expect(p, TOK_LT, "expected '<' after 'ptr'");
        AstType *inner = parse_type(p);
        expect(p, TOK_GT, "expected '>' after ptr type");
        AstType *t = ast_type_new(p->arena, AST_TYPE_PTR, start);
        t->ptr.inner = inner;
        t->ptr.is_volatile = false;
        return t;
    }

    /* own<T> */
    if (match(p, TOK_KW_OWN)) {
        expect(p, TOK_LT, "expected '<' after 'own'");
        AstType *inner = parse_type(p);
        expect(p, TOK_GT, "expected '>' after own type");
        AstType *t = ast_type_new(p->arena, AST_TYPE_OWN, start);
        t->own.inner = inner;
        return t;
    }

    /* slice<T> */
    if (match(p, TOK_KW_SLICE)) {
        expect(p, TOK_LT, "expected '<' after 'slice'");
        AstType *inner = parse_type(p);
        expect(p, TOK_GT, "expected '>' after slice type");
        AstType *t = ast_type_new(p->arena, AST_TYPE_SLICE, start);
        t->slice.element = inner;
        return t;
    }

    /* Array: [T; N] */
    if (match(p, TOK_LBRACKET)) {
        AstType *elem = parse_type(p);
        expect(p, TOK_SEMICOLON, "expected ';' in array type [T; N]");
        AstNode *size = parse_expression(p);
        expect(p, TOK_RBRACKET, "expected ']' after array type");
        AstType *t = ast_type_new(p->arena, AST_TYPE_ARRAY, start);
        t->array.element = elem;
        t->array.size_expr = size;
        return t;
    }

    /* Primitive types */
    if (is_primitive_type(p->current.kind)) {
        TokenKind prim = p->current.kind;
        advance(p);
        AstType *t = ast_type_new(p->arena, AST_TYPE_PRIMITIVE, start);
        t->primitive = prim;
        return t;
    }

    /* Named type or path type (with optional generic args: T<A, B>) */
    if (check(p, TOK_IDENTIFIER) || check(p, TOK_KW_SELF_TYPE)) {
        StringView name;
        if (check(p, TOK_KW_SELF_TYPE)) {
            name = sv_from_cstr("Self");
        } else {
            name = sv_from_parts(p->current.ident.data, p->current.ident.length);
        }
        advance(p);
        AstType *t = ast_type_new(p->arena, AST_TYPE_NAMED, start);
        t->named.name = name;
        t->named.path = NULL;

        /* Generic args: Name<T, U, N> */
        if (check(p, TOK_LT)) {
            advance(p); /* consume '<' */
            AstType *args[32];
            u32 arg_count = 0;
            while (!check(p, TOK_GT) && !check(p, TOK_EOF)) {
                if (arg_count > 0) expect(p, TOK_COMMA, "expected ',' in generic args");
                /* Allow expressions as generic args (for const generics like 256) */
                if (is_primitive_type(p->current.kind) || check(p, TOK_IDENTIFIER) ||
                    check(p, TOK_KW_SELF_TYPE) || check(p, TOK_AMP) ||
                    check(p, TOK_KW_PTR) || check(p, TOK_KW_OWN) ||
                    check(p, TOK_KW_SLICE) || check(p, TOK_LBRACKET) ||
                    check(p, TOK_KW_FN) || check(p, TOK_BANG)) {
                    args[arg_count++] = parse_type(p);
                } else {
                    /* Const generic: treat as type wrapping expression */
                    AstType *ct = ast_type_new(p->arena, AST_TYPE_INFERRED, p->current.span);
                    args[arg_count++] = ct;
                    /* Skip the expression token(s) */
                    advance(p);
                }
            }
            expect(p, TOK_GT, "expected '>' after generic args");
            AstType *gt = ast_type_new(p->arena, AST_TYPE_GENERIC, start);
            gt->generic.base = t;
            gt->generic.args = (AstType **)arena_alloc(p->arena, arg_count * sizeof(AstType*));
            memcpy(gt->generic.args, args, arg_count * sizeof(AstType*));
            gt->generic.arg_count = arg_count;
            return gt;
        }
        return t;
    }

    /* fn pointer: fn(A, B) -> C */
    if (match(p, TOK_KW_FN)) {
        expect(p, TOK_LPAREN, "expected '(' in fn type");
        /* Parse param types */
        AstType *params[64];
        u32 count = 0;
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            if (count > 0) expect(p, TOK_COMMA, "expected ','");
            params[count++] = parse_type(p);
        }
        expect(p, TOK_RPAREN, "expected ')'");
        AstType *ret = NULL;
        if (match(p, TOK_ARROW)) ret = parse_type(p);
        AstType *t = ast_type_new(p->arena, AST_TYPE_FN_PTR, start);
        t->fn_ptr.params = (AstType **)arena_alloc(p->arena, count * sizeof(AstType*));
        memcpy(t->fn_ptr.params, params, count * sizeof(AstType*));
        t->fn_ptr.param_count = count;
        t->fn_ptr.return_type = ret;
        return t;
    }

    error_current(p, "expected type");
    return ast_type_new(p->arena, AST_TYPE_INFERRED, start);
}

/* ── Expression parsing (Pratt parser) ───────────────────────── */

static int get_precedence(TokenKind kind) {
    switch (kind) {
        case TOK_KW_OR:                             return 1;
        case TOK_KW_AND:                            return 2;
        case TOK_EQ_EQ: case TOK_NOT_EQ:            return 3;
        case TOK_LT: case TOK_GT:
        case TOK_LT_EQ: case TOK_GT_EQ:             return 4;
        case TOK_PIPE:                               return 5;
        case TOK_CARET:                              return 6;
        case TOK_AMP:                                return 7;
        case TOK_SHL: case TOK_SHR:                  return 8;
        case TOK_DOT_DOT: case TOK_DOT_DOT_EQ:      return 9;
        case TOK_PLUS: case TOK_MINUS:               return 10;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return 11;
        case TOK_KW_AS:                              return 12;
        default:                                     return 0;
    }
}

static BinaryOp token_to_binop(TokenKind kind) {
    switch (kind) {
        case TOK_PLUS:    return BIN_ADD;
        case TOK_MINUS:   return BIN_SUB;
        case TOK_STAR:    return BIN_MUL;
        case TOK_SLASH:   return BIN_DIV;
        case TOK_PERCENT: return BIN_MOD;
        case TOK_AMP:     return BIN_BIT_AND;
        case TOK_PIPE:    return BIN_BIT_OR;
        case TOK_CARET:   return BIN_BIT_XOR;
        case TOK_SHL:     return BIN_SHL;
        case TOK_SHR:     return BIN_SHR;
        case TOK_EQ_EQ:   return BIN_EQ;
        case TOK_NOT_EQ:  return BIN_NEQ;
        case TOK_LT:      return BIN_LT;
        case TOK_GT:      return BIN_GT;
        case TOK_LT_EQ:   return BIN_LT_EQ;
        case TOK_GT_EQ:   return BIN_GT_EQ;
        case TOK_KW_AND:  return BIN_AND;
        case TOK_KW_OR:   return BIN_OR;
        case TOK_DOT_DOT: return BIN_RANGE;
        case TOK_DOT_DOT_EQ: return BIN_RANGE_INCLUSIVE;
        default:          return BIN_ADD;
    }
}

static AstNode *parse_primary(Parser *p) {
    Span start = p->current.span;

    /* Integer literal */
    if (match(p, TOK_INT_LITERAL)) {
        AstNode *n = ast_new(p->arena, AST_INT_LIT, start);
        n->int_lit.value = p->previous.int_lit.value;
        n->int_lit.base = p->previous.int_lit.base;
        return n;
    }

    /* Float literal */
    if (match(p, TOK_FLOAT_LITERAL)) {
        AstNode *n = ast_new(p->arena, AST_FLOAT_LIT, start);
        n->float_lit.value = p->previous.float_lit;
        return n;
    }

    /* String literal */
    if (match(p, TOK_STRING_LITERAL) || match(p, TOK_RAW_STRING_LITERAL)) {
        AstNode *n = ast_new(p->arena, AST_STRING_LIT, start);
        n->string_lit.value = sv_from_parts(p->previous.str_lit.data, p->previous.str_lit.length);
        n->string_lit.is_raw = p->previous.str_lit.is_raw;
        return n;
    }

    /* Char literal */
    if (match(p, TOK_CHAR_LITERAL)) {
        AstNode *n = ast_new(p->arena, AST_CHAR_LIT, start);
        n->char_lit.value = p->previous.char_lit;
        return n;
    }

    /* true / false */
    if (match(p, TOK_KW_TRUE))  { AstNode *n = ast_new(p->arena, AST_BOOL_LIT, start); n->bool_lit.value = true; return n; }
    if (match(p, TOK_KW_FALSE)) { AstNode *n = ast_new(p->arena, AST_BOOL_LIT, start); n->bool_lit.value = false; return n; }

    /* null */
    if (match(p, TOK_KW_NULL)) return ast_new(p->arena, AST_NULL_LIT, start);

    /* Parenthesized expression */
    if (match(p, TOK_LPAREN)) {
        AstNode *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "expected ')'");
        return expr;
    }

    /* Block expression */
    if (check(p, TOK_LBRACE)) return parse_block(p);

    /* if expression */
    if (match(p, TOK_KW_IF)) {
        AstNode *cond = parse_expression(p);
        AstNode *then_b = parse_block(p);
        AstNode *else_b = NULL;
        if (match(p, TOK_KW_ELSE)) {
            if (check(p, TOK_KW_IF)) else_b = parse_primary(p); /* allow match on 'if' again */
            else else_b = parse_block(p);
        }
        AstNode *n = ast_new(p->arena, AST_IF, start);
        n->if_expr.condition = cond;
        n->if_expr.then_block = then_b;
        n->if_expr.else_block = else_b;
        return n;
    }

    /* loop */
    if (match(p, TOK_KW_LOOP)) {
        AstNode *body = parse_block(p);
        AstNode *n = ast_new(p->arena, AST_LOOP, start);
        n->loop_expr.body = body;
        return n;
    }

    /* while */
    if (match(p, TOK_KW_WHILE)) {
        AstNode *cond = parse_expression(p);
        AstNode *body = parse_block(p);
        AstNode *n = ast_new(p->arena, AST_WHILE, start);
        n->while_loop.condition = cond;
        n->while_loop.body = body;
        return n;
    }

    /* for */
    if (match(p, TOK_KW_FOR)) {
        StringView var = sv_from_parts(p->current.ident.data, p->current.ident.length);
        expect(p, TOK_IDENTIFIER, "expected variable name in for");
        expect(p, TOK_KW_IN, "expected 'in' after for variable");
        AstNode *iter = parse_expression(p);
        AstNode *body = parse_block(p);
        AstNode *n = ast_new(p->arena, AST_FOR, start);
        n->for_loop.var_name = var;
        n->for_loop.iterable = iter;
        n->for_loop.body = body;
        return n;
    }

    /* unsafe block */
    if (match(p, TOK_KW_UNSAFE)) {
        AstNode *body = parse_block(p);
        AstNode *n = ast_new(p->arena, AST_UNSAFE_BLOCK, start);
        n->unsafe_block.body = body;
        return n;
    }

    /* Unary: - ~ not & * */
    if (match(p, TOK_MINUS))   { AstNode *n = ast_new(p->arena, AST_UNARY_OP, start); n->unary.op = UNARY_NEG; n->unary.operand = parse_primary(p); return n; }
    if (match(p, TOK_TILDE))   { AstNode *n = ast_new(p->arena, AST_UNARY_OP, start); n->unary.op = UNARY_BIT_NOT; n->unary.operand = parse_primary(p); return n; }
    if (match(p, TOK_KW_NOT))  { AstNode *n = ast_new(p->arena, AST_UNARY_OP, start); n->unary.op = UNARY_LOG_NOT; n->unary.operand = parse_primary(p); return n; }
    if (match(p, TOK_STAR))    { AstNode *n = ast_new(p->arena, AST_UNARY_OP, start); n->unary.op = UNARY_DEREF; n->unary.operand = parse_primary(p); return n; }
    if (match(p, TOK_AMP)) {
        bool is_mut = match(p, TOK_KW_MUT);
        AstNode *n = ast_new(p->arena, AST_UNARY_OP, start);
        n->unary.op = is_mut ? UNARY_REF_MUT : UNARY_REF;
        n->unary.operand = parse_primary(p);
        return n;
    }

    /* Array literal: [a, b, c] */
    if (match(p, TOK_LBRACKET)) {
        AstNode *elems[256];
        u32 count = 0;
        while (!check(p, TOK_RBRACKET) && !check(p, TOK_EOF)) {
            if (count > 0) expect(p, TOK_COMMA, "expected ','");
            elems[count++] = parse_expression(p);
            /* Check for [val; count] */
            if (count == 1 && match(p, TOK_SEMICOLON)) {
                AstNode *repeat_count = parse_expression(p);
                expect(p, TOK_RBRACKET, "expected ']'");
                AstNode *n = ast_new(p->arena, AST_ARRAY_LIT, start);
                n->array_lit.repeat_value = elems[0];
                n->array_lit.repeat_count = repeat_count;
                n->array_lit.elements = NULL;
                n->array_lit.element_count = 0;
                return n;
            }
        }
        expect(p, TOK_RBRACKET, "expected ']'");
        AstNode *n = ast_new(p->arena, AST_ARRAY_LIT, start);
        n->array_lit.elements = ast_node_array(p->arena, count);
        memcpy(n->array_lit.elements, elems, count * sizeof(AstNode*));
        n->array_lit.element_count = count;
        return n;
    }

    /* asm! and other macros handled by the identifier block below */
    if (check(p, TOK_IDENTIFIER) || check(p, TOK_KW_SELF) ||
        check(p, TOK_KW_KERNEL) || check(p, TOK_KW_PORT) ||
        check(p, TOK_KW_ASM)) {
        StringView name;
        if (check(p, TOK_IDENTIFIER))
            name = sv_from_parts(p->current.ident.data, p->current.ident.length);
        else
            name = sv_from_cstr(token_kind_to_str(p->current.kind));
        advance(p);

        /* ident followed by ! means macro call (like asm!, panic! etc) */
        if (check(p, TOK_BANG)) {
            advance(p); /* consume '!' */
            expect(p, TOK_LPAREN, "expected '(' after macro name");
            AstNode *n = ast_new(p->arena, AST_ASM_EXPR, start);
            if (check(p, TOK_STRING_LITERAL)) {
                n->asm_expr.template_str = sv_from_parts(p->current.str_lit.data, p->current.str_lit.length);
                advance(p);
            }
            /* Skip remaining args */
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) advance(p);
            expect(p, TOK_RPAREN, "expected ')'");
            return n;
        }

        /* Check for path: a::b::c */
        if (check(p, TOK_COLON_COLON)) {
            StringView segs[32];
            u32 seg_count = 0;
            segs[seg_count++] = name;
            while (match(p, TOK_COLON_COLON)) {
                if (check(p, TOK_IDENTIFIER)) {
                    segs[seg_count++] = sv_from_parts(p->current.ident.data, p->current.ident.length);
                } else {
                    segs[seg_count++] = sv_from_cstr(token_kind_to_str(p->current.kind));
                }
                advance(p);
            }
            AstNode *n = ast_new(p->arena, AST_PATH, start);
            n->path.segments = (StringView *)arena_alloc(p->arena, seg_count * sizeof(StringView));
            memcpy(n->path.segments, segs, seg_count * sizeof(StringView));
            n->path.segment_count = seg_count;
            return n;
        }

        AstNode *n = ast_new(p->arena, AST_IDENT, start);
        n->ident.name = name;
        return n;
    }

    /* (asm! already handled above) */

    error_current(p, "expected expression");
    advance(p);
    return ast_new(p->arena, AST_INT_LIT, start); /* dummy */
}

/* Postfix: calls, field access, indexing, ? */
static AstNode *parse_postfix(Parser *p) {
    AstNode *expr = parse_primary(p);
    for (;;) {
        Span start = p->current.span;
        /* Function call: expr(args) */
        if (match(p, TOK_LPAREN)) {
            AstNode *args[128];
            u32 count = 0;
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                if (count > 0) expect(p, TOK_COMMA, "expected ','");
                args[count++] = parse_expression(p);
            }
            expect(p, TOK_RPAREN, "expected ')'");
            AstNode *n = ast_new(p->arena, AST_CALL, start);
            n->call.callee = expr;
            n->call.args = ast_node_array(p->arena, count);
            memcpy(n->call.args, args, count * sizeof(AstNode*));
            n->call.arg_count = count;
            expr = n;
        }
        /* Field access / method: expr.name */
        else if (match(p, TOK_DOT)) {
            StringView fname;
            if (check(p, TOK_IDENTIFIER))
                fname = sv_from_parts(p->current.ident.data, p->current.ident.length);
            else
                fname = sv_from_cstr(token_kind_to_str(p->current.kind));
            advance(p);
            if (check(p, TOK_LPAREN)) {
                /* Method call */
                advance(p);
                AstNode *args[128];
                u32 count = 0;
                while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
                    if (count > 0) expect(p, TOK_COMMA, "expected ','");
                    args[count++] = parse_expression(p);
                }
                expect(p, TOK_RPAREN, "expected ')'");
                AstNode *n = ast_new(p->arena, AST_METHOD_CALL, start);
                n->method_call.object = expr;
                n->method_call.method_name = fname;
                n->method_call.args = ast_node_array(p->arena, count);
                memcpy(n->method_call.args, args, count * sizeof(AstNode*));
                n->method_call.arg_count = count;
                expr = n;
            } else {
                AstNode *n = ast_new(p->arena, AST_FIELD_ACCESS, start);
                n->field_access.object = expr;
                n->field_access.field_name = fname;
                expr = n;
            }
        }
        /* Index: expr[i] */
        else if (match(p, TOK_LBRACKET)) {
            AstNode *idx = parse_expression(p);
            expect(p, TOK_RBRACKET, "expected ']'");
            AstNode *n = ast_new(p->arena, AST_INDEX, start);
            n->index.object = expr;
            n->index.index = idx;
            expr = n;
        }
        /* Error propagation: expr? */
        else if (match(p, TOK_QUESTION)) {
            AstNode *n = ast_new(p->arena, AST_ERROR_PROP, start);
            n->error_prop.expr = expr;
            expr = n;
        }
        else break;
    }
    return expr;
}

/* Binary expression with precedence climbing */
static AstNode *parse_binary(Parser *p, int min_prec) {
    AstNode *left = parse_postfix(p);
    for (;;) {
        int prec = get_precedence(p->current.kind);
        if (prec <= min_prec) break;
        /* Cast: expr as Type */
        if (match(p, TOK_KW_AS)) {
            AstType *target = parse_type(p);
            AstNode *n = ast_new(p->arena, AST_CAST, left->span);
            n->cast.expr = left;
            n->cast.target_type = target;
            left = n;
            continue;
        }
        TokenKind op_tok = p->current.kind;
        advance(p);
        AstNode *right = parse_binary(p, prec);
        AstNode *n = ast_new(p->arena, AST_BINARY_OP, left->span);
        n->binary.op = token_to_binop(op_tok);
        n->binary.left = left;
        n->binary.right = right;
        left = n;
    }
    return left;
}

static AstNode *parse_expression(Parser *p) {
    return parse_binary(p, 0);
}

/* ── Block parsing ───────────────────────────────────────────── */

static AstNode *parse_block(Parser *p) {
    Span start = p->current.span;
    expect(p, TOK_LBRACE, "expected '{'");
    AstNode *stmts[512];
    u32 count = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        stmts[count++] = parse_statement(p);
        if (count >= 512) break;
    }
    expect(p, TOK_RBRACE, "expected '}'");
    AstNode *n = ast_new(p->arena, AST_BLOCK, start);
    n->block.stmts = ast_node_array(p->arena, count);
    memcpy(n->block.stmts, stmts, count * sizeof(AstNode*));
    n->block.stmt_count = count;
    n->block.final_expr = NULL;
    return n;
}

/* ── Statement parsing ───────────────────────────────────────── */

static AstNode *parse_let_stmt(Parser *p) {
    Span start = p->previous.span;
    bool is_mut = match(p, TOK_KW_MUT);
    StringView name = {"<error>", 7};
    if (check(p, TOK_IDENTIFIER))
        name = sv_from_parts(p->current.ident.data, p->current.ident.length);
    expect(p, TOK_IDENTIFIER, "expected variable name");
    AstType *type = NULL;
    if (match(p, TOK_COLON)) type = parse_type(p);
    expect(p, TOK_EQ, "expected '=' in let");
    AstNode *init = parse_expression(p);
    expect(p, TOK_SEMICOLON, "expected ';'");
    AstNode *n = ast_new(p->arena, AST_LET_STMT, start);
    n->let_stmt.name = name;
    n->let_stmt.type = type;
    n->let_stmt.init = init;
    n->let_stmt.is_mut = is_mut;
    return n;
}

static bool is_assign_op(TokenKind k) {
    return k == TOK_EQ || k == TOK_PLUS_EQ || k == TOK_MINUS_EQ ||
           k == TOK_STAR_EQ || k == TOK_SLASH_EQ || k == TOK_PERCENT_EQ ||
           k == TOK_AMP_EQ || k == TOK_PIPE_EQ || k == TOK_CARET_EQ ||
           k == TOK_SHL_EQ || k == TOK_SHR_EQ;
}

static AssignOp token_to_assign(TokenKind k) {
    switch (k) {
        case TOK_EQ:         return ASSIGN_EQ;
        case TOK_PLUS_EQ:    return ASSIGN_ADD;
        case TOK_MINUS_EQ:   return ASSIGN_SUB;
        case TOK_STAR_EQ:    return ASSIGN_MUL;
        case TOK_SLASH_EQ:   return ASSIGN_DIV;
        case TOK_PERCENT_EQ: return ASSIGN_MOD;
        case TOK_AMP_EQ:     return ASSIGN_BIT_AND;
        case TOK_PIPE_EQ:    return ASSIGN_BIT_OR;
        case TOK_CARET_EQ:   return ASSIGN_BIT_XOR;
        case TOK_SHL_EQ:     return ASSIGN_SHL;
        case TOK_SHR_EQ:     return ASSIGN_SHR;
        default:             return ASSIGN_EQ;
    }
}

static AstNode *parse_statement(Parser *p) {
    Span start = p->current.span;

    if (match(p, TOK_KW_LET)) return parse_let_stmt(p);

    if (match(p, TOK_KW_RETURN)) {
        AstNode *val = NULL;
        if (!check(p, TOK_SEMICOLON)) val = parse_expression(p);
        expect(p, TOK_SEMICOLON, "expected ';' after return");
        AstNode *n = ast_new(p->arena, AST_RETURN_STMT, start);
        n->return_stmt.value = val;
        return n;
    }

    if (match(p, TOK_KW_BREAK)) {
        expect(p, TOK_SEMICOLON, "expected ';'");
        return ast_new(p->arena, AST_BREAK_STMT, start);
    }
    if (match(p, TOK_KW_CONTINUE)) {
        expect(p, TOK_SEMICOLON, "expected ';'");
        return ast_new(p->arena, AST_CONTINUE_STMT, start);
    }
    if (match(p, TOK_KW_DEFER)) {
        AstNode *expr = parse_expression(p);
        expect(p, TOK_SEMICOLON, "expected ';'");
        AstNode *n = ast_new(p->arena, AST_DEFER_STMT, start);
        n->defer_stmt.expr = expr;
        return n;
    }

    /* Expression statement or assignment */
    AstNode *expr = parse_expression(p);
    if (is_assign_op(p->current.kind)) {
        AssignOp op = token_to_assign(p->current.kind);
        advance(p);
        AstNode *val = parse_expression(p);
        expect(p, TOK_SEMICOLON, "expected ';'");
        AstNode *n = ast_new(p->arena, AST_ASSIGN_STMT, start);
        n->assign_stmt.target = expr;
        n->assign_stmt.value = val;
        n->assign_stmt.op = op;
        return n;
    }
    expect(p, TOK_SEMICOLON, "expected ';'");
    AstNode *n = ast_new(p->arena, AST_EXPR_STMT, start);
    n->expr_stmt.expr = expr;
    return n;
}

/* ── Top-level declaration parsing ───────────────────────────── */

static AstAttribute *parse_attributes(Parser *p, u32 *out_count) {
    AstAttribute attrs[32];
    u32 count = 0;
    while (check(p, TOK_HASH)) {
        advance(p);
        expect(p, TOK_LBRACKET, "expected '[' after '#'");
        attrs[count].span = p->current.span;
        if (check(p, TOK_IDENTIFIER)) {
            attrs[count].name = sv_from_parts(p->current.ident.data, p->current.ident.length);
        } else {
            attrs[count].name = sv_from_cstr(token_kind_to_str(p->current.kind));
        }
        advance(p);
        attrs[count].args = NULL; attrs[count].arg_count = 0;
        /* Skip optional args in parens */
        if (match(p, TOK_LPAREN)) {
            while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) advance(p);
            expect(p, TOK_RPAREN, "expected ')'");
        }
        expect(p, TOK_RBRACKET, "expected ']'");
        count++;
    }
    *out_count = count;
    if (count == 0) return NULL;
    AstAttribute *result = (AstAttribute *)arena_alloc(p->arena, count * sizeof(AstAttribute));
    memcpy(result, attrs, count * sizeof(AstAttribute));
    return result;
}

static AstNode *parse_fn_decl(Parser *p, bool is_pub, AstAttribute *attribs, u32 attrib_count) {
    Span start = p->previous.span;
    bool is_interrupt = false;
    /* Check for interrupt fn */
    if (p->previous.kind == TOK_KW_INTERRUPT) {
        is_interrupt = true;
        expect(p, TOK_KW_FN, "expected 'fn' after 'interrupt'");
    }

    StringView name = {"<error>", 7};
    if (check(p, TOK_IDENTIFIER))
        name = sv_from_parts(p->current.ident.data, p->current.ident.length);
    expect(p, TOK_IDENTIFIER, "expected function name");
    expect(p, TOK_LPAREN, "expected '('");

    AstParam params[64];
    u32 param_count = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        if (param_count > 0) expect(p, TOK_COMMA, "expected ','");
        params[param_count].span = p->current.span;
        /* Handle self param */
        if (check(p, TOK_KW_SELF) || (check(p, TOK_AMP) /* &self */)) {
            if (match(p, TOK_AMP)) {
                match(p, TOK_KW_MUT);
                params[param_count].name = sv_from_cstr("self");
                params[param_count].type = NULL;
                expect(p, TOK_KW_SELF, "expected 'self'");
            } else {
                params[param_count].name = sv_from_cstr("self");
                params[param_count].type = NULL;
                advance(p);
            }
            /* self: &mut Self */
            if (match(p, TOK_COLON)) {
                params[param_count].type = parse_type(p);
            }
        } else {
            params[param_count].name = check(p, TOK_IDENTIFIER)
                ? sv_from_parts(p->current.ident.data, p->current.ident.length)
                : (StringView){"<error>", 7};
            expect(p, TOK_IDENTIFIER, "expected parameter name");
            expect(p, TOK_COLON, "expected ':' after parameter name");
            params[param_count].type = parse_type(p);
        }
        param_count++;
    }
    expect(p, TOK_RPAREN, "expected ')'");

    AstType *ret = NULL;
    if (match(p, TOK_ARROW)) ret = parse_type(p);

    AstNode *body = NULL;
    if (check(p, TOK_LBRACE)) body = parse_block(p);
    else expect(p, TOK_SEMICOLON, "expected '{' or ';'");

    AstNode *n = ast_new(p->arena, AST_FN_DECL, start);
    n->fn_decl.name = name;
    n->fn_decl.params = (AstParam *)arena_alloc(p->arena, param_count * sizeof(AstParam));
    memcpy(n->fn_decl.params, params, param_count * sizeof(AstParam));
    n->fn_decl.param_count = param_count;
    n->fn_decl.return_type = ret;
    n->fn_decl.body = body;
    n->fn_decl.attribs = attribs;
    n->fn_decl.attrib_count = attrib_count;
    n->fn_decl.is_pub = is_pub;
    n->fn_decl.is_interrupt = is_interrupt;
    return n;
}

static AstNode *parse_struct_decl(Parser *p, bool is_pub, AstAttribute *attribs, u32 ac) {
    Span start = p->previous.span;
    StringView name = check(p, TOK_IDENTIFIER)
        ? sv_from_parts(p->current.ident.data, p->current.ident.length)
        : (StringView){"<error>", 7};
    expect(p, TOK_IDENTIFIER, "expected struct name");
    expect(p, TOK_LBRACE, "expected '{'");
    AstField fields[128];
    u32 count = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        fields[count].span = p->current.span;
        fields[count].is_pub = match(p, TOK_KW_PUB);
        fields[count].name = check(p, TOK_IDENTIFIER)
            ? sv_from_parts(p->current.ident.data, p->current.ident.length)
            : (StringView){"<error>", 7};
        expect(p, TOK_IDENTIFIER, "expected field name");
        expect(p, TOK_COLON, "expected ':'");
        fields[count].type = parse_type(p);
        count++;
        if (!match(p, TOK_COMMA)) break;
    }
    expect(p, TOK_RBRACE, "expected '}'");
    AstNode *n = ast_new(p->arena, AST_STRUCT_DECL, start);
    n->struct_decl.name = name;
    n->struct_decl.fields = (AstField *)arena_alloc(p->arena, count * sizeof(AstField));
    memcpy(n->struct_decl.fields, fields, count * sizeof(AstField));
    n->struct_decl.field_count = count;
    n->struct_decl.attribs = attribs;
    n->struct_decl.attrib_count = ac;
    n->struct_decl.is_pub = is_pub;
    return n;
}

static AstNode *parse_module_decl(Parser *p) {
    Span start = p->previous.span;
    AstNode *path = parse_expression(p);
    expect(p, TOK_SEMICOLON, "expected ';'");
    AstNode *n = ast_new(p->arena, AST_MODULE_DECL, start);
    n->module_decl.path = path;
    return n;
}

static AstNode *parse_import_decl(Parser *p) {
    Span start = p->previous.span;
    AstNode *path = parse_expression(p);
    StringView items[64];
    u32 item_count = 0;
    if (match(p, TOK_LBRACE)) {
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            if (item_count > 0) expect(p, TOK_COMMA, "expected ','");
            if (check(p, TOK_IDENTIFIER))
                items[item_count] = sv_from_parts(p->current.ident.data, p->current.ident.length);
            else
                items[item_count] = sv_from_cstr(token_kind_to_str(p->current.kind));
            advance(p);
            item_count++;
        }
        expect(p, TOK_RBRACE, "expected '}'");
    }
    expect(p, TOK_SEMICOLON, "expected ';'");
    AstNode *n = ast_new(p->arena, AST_IMPORT_DECL, start);
    n->import_decl.path = path;
    n->import_decl.items = (StringView *)arena_alloc(p->arena, item_count * sizeof(StringView));
    memcpy(n->import_decl.items, items, item_count * sizeof(StringView));
    n->import_decl.item_count = item_count;
    return n;
}

static AstNode *parse_const_decl(Parser *p, bool is_pub) {
    Span start = p->previous.span;
    StringView name = sv_from_parts(p->current.ident.data, p->current.ident.length);
    expect(p, TOK_IDENTIFIER, "expected constant name");
    expect(p, TOK_COLON, "expected ':'");
    AstType *type = parse_type(p);
    expect(p, TOK_EQ, "expected '='");
    AstNode *val = parse_expression(p);
    expect(p, TOK_SEMICOLON, "expected ';'");
    AstNode *n = ast_new(p->arena, AST_CONST_DECL, start);
    n->const_decl.name = name;
    n->const_decl.type = type;
    n->const_decl.value = val;
    n->const_decl.is_pub = is_pub;
    return n;
}

static AstNode *parse_driver_decl(Parser *p, bool is_pub) {
    Span start = p->previous.span;
    StringView name = check(p, TOK_IDENTIFIER)
        ? sv_from_parts(p->current.ident.data, p->current.ident.length)
        : (StringView){"<error>", 7};
    expect(p, TOK_IDENTIFIER, "expected driver name");
    expect(p, TOK_LBRACE, "expected '{'");
    AstField fields[64]; u32 fc = 0;
    AstNode *methods[128]; u32 mc = 0;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        u32 ac = 0;
        AstAttribute *attribs = parse_attributes(p, &ac);
        bool pub = match(p, TOK_KW_PUB);
        if (check(p, TOK_KW_FN) || check(p, TOK_KW_INTERRUPT)) {
            if (match(p, TOK_KW_FN) || match(p, TOK_KW_INTERRUPT))
                methods[mc++] = parse_fn_decl(p, pub, attribs, ac);
        } else {
            fields[fc].span = p->current.span;
            fields[fc].is_pub = pub;
            fields[fc].name = check(p, TOK_IDENTIFIER)
                ? sv_from_parts(p->current.ident.data, p->current.ident.length)
                : (StringView){"<error>", 7};
            expect(p, TOK_IDENTIFIER, "expected field name");
            expect(p, TOK_COLON, "expected ':'");
            fields[fc].type = parse_type(p);
            fc++;
            if (!match(p, TOK_COMMA)) { /* optional comma */ }
        }
    }
    expect(p, TOK_RBRACE, "expected '}'");
    AstNode *n = ast_new(p->arena, AST_DRIVER_DECL, start);
    n->driver_decl.name = name;
    n->driver_decl.fields = (AstField *)arena_alloc(p->arena, fc * sizeof(AstField));
    memcpy(n->driver_decl.fields, fields, fc * sizeof(AstField));
    n->driver_decl.field_count = fc;
    n->driver_decl.methods = ast_node_array(p->arena, mc);
    memcpy(n->driver_decl.methods, methods, mc * sizeof(AstNode*));
    n->driver_decl.method_count = mc;
    n->driver_decl.is_pub = is_pub;
    return n;
}

static AstNode *parse_syscall_decl(Parser *p) {
    Span start = p->previous.span;
    StringView name = check(p, TOK_IDENTIFIER)
        ? sv_from_parts(p->current.ident.data, p->current.ident.length)
        : (StringView){"<error>", 7};
    expect(p, TOK_IDENTIFIER, "expected syscall name");
    expect(p, TOK_LPAREN, "expected '('");
    AstParam params[32]; u32 pc = 0;
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        if (pc > 0) expect(p, TOK_COMMA, "expected ','");
        params[pc].span = p->current.span;
        params[pc].name = check(p, TOK_IDENTIFIER)
            ? sv_from_parts(p->current.ident.data, p->current.ident.length)
            : (StringView){"<error>", 7};
        expect(p, TOK_IDENTIFIER, "expected param name");
        expect(p, TOK_COLON, "expected ':'");
        params[pc].type = parse_type(p);
        pc++;
    }
    expect(p, TOK_RPAREN, "expected ')'");
    AstType *ret = NULL;
    if (match(p, TOK_ARROW)) ret = parse_type(p);
    AstNode *body = parse_block(p);
    AstNode *n = ast_new(p->arena, AST_SYSCALL_DECL, start);
    n->syscall_decl.name = name;
    n->syscall_decl.params = (AstParam *)arena_alloc(p->arena, pc * sizeof(AstParam));
    memcpy(n->syscall_decl.params, params, pc * sizeof(AstParam));
    n->syscall_decl.param_count = pc;
    n->syscall_decl.return_type = ret;
    n->syscall_decl.body = body;
    return n;
}

static AstNode *parse_declaration(Parser *p) {
    u32 ac = 0;
    AstAttribute *attribs = parse_attributes(p, &ac);
    bool is_pub = match(p, TOK_KW_PUB);

    if (match(p, TOK_KW_FN))        return parse_fn_decl(p, is_pub, attribs, ac);
    if (match(p, TOK_KW_STRUCT))    return parse_struct_decl(p, is_pub, attribs, ac);
    if (match(p, TOK_KW_MODULE))    return parse_module_decl(p);
    if (match(p, TOK_KW_IMPORT))    return parse_import_decl(p);
    if (match(p, TOK_KW_CONST))     return parse_const_decl(p, is_pub);
    if (match(p, TOK_KW_DRIVER))    return parse_driver_decl(p, is_pub);
    if (match(p, TOK_KW_SYSCALL))   return parse_syscall_decl(p);
    if (match(p, TOK_KW_INTERRUPT)) return parse_fn_decl(p, is_pub, attribs, ac);

    /* Fallback: try statement */
    AstNode *stmt = parse_statement(p);
    if (p->panic_mode) synchronize(p);
    return stmt;
}

/* ── Public API ──────────────────────────────────────────────── */

void parser_init(Parser *p, Lexer *lexer, Arena *arena) {
    p->lexer = lexer;
    p->arena = arena;
    p->source_id = lexer->source_id;
    p->had_error = false;
    p->panic_mode = false;
    advance(p); /* prime first token */
}

AstNode *parser_parse(Parser *p) {
    Span start = p->current.span;
    AstNode *decls[1024];
    u32 count = 0;
    while (!check(p, TOK_EOF)) {
        decls[count++] = parse_declaration(p);
        if (count >= 1024) break;
    }
    AstNode *prog = ast_new(p->arena, AST_PROGRAM, start);
    prog->program.decls = ast_node_array(p->arena, count);
    memcpy(prog->program.decls, decls, count * sizeof(AstNode*));
    prog->program.decl_count = count;
    return prog;
}

bool parser_had_error(const Parser *p) {
    return p->had_error;
}

/* ── AST debug helpers ───────────────────────────────────────── */

const char *ast_node_kind_str(AstNodeKind kind) {
    switch (kind) {
        case AST_PROGRAM:       return "Program";
        case AST_MODULE_DECL:   return "ModuleDecl";
        case AST_IMPORT_DECL:   return "ImportDecl";
        case AST_FN_DECL:       return "FnDecl";
        case AST_STRUCT_DECL:   return "StructDecl";
        case AST_ENUM_DECL:     return "EnumDecl";
        case AST_CONST_DECL:    return "ConstDecl";
        case AST_STATIC_DECL:   return "StaticDecl";
        case AST_DRIVER_DECL:   return "DriverDecl";
        case AST_SYSCALL_DECL:  return "SyscallDecl";
        case AST_LET_STMT:      return "LetStmt";
        case AST_EXPR_STMT:     return "ExprStmt";
        case AST_RETURN_STMT:   return "ReturnStmt";
        case AST_ASSIGN_STMT:   return "AssignStmt";
        case AST_BREAK_STMT:    return "BreakStmt";
        case AST_CONTINUE_STMT: return "ContinueStmt";
        case AST_DEFER_STMT:    return "DeferStmt";
        case AST_INT_LIT:       return "IntLit";
        case AST_FLOAT_LIT:     return "FloatLit";
        case AST_STRING_LIT:    return "StringLit";
        case AST_CHAR_LIT:      return "CharLit";
        case AST_BOOL_LIT:      return "BoolLit";
        case AST_NULL_LIT:      return "NullLit";
        case AST_IDENT:         return "Ident";
        case AST_PATH:          return "Path";
        case AST_BINARY_OP:     return "BinaryOp";
        case AST_UNARY_OP:      return "UnaryOp";
        case AST_CALL:          return "Call";
        case AST_METHOD_CALL:   return "MethodCall";
        case AST_FIELD_ACCESS:  return "FieldAccess";
        case AST_INDEX:         return "Index";
        case AST_CAST:          return "Cast";
        case AST_BLOCK:         return "Block";
        case AST_IF:            return "If";
        case AST_MATCH:         return "Match";
        case AST_FOR:           return "For";
        case AST_WHILE:         return "While";
        case AST_LOOP:          return "Loop";
        case AST_UNSAFE_BLOCK:  return "UnsafeBlock";
        case AST_ASM_EXPR:      return "AsmExpr";
        case AST_ERROR_PROP:    return "ErrorProp";
        case AST_ARRAY_LIT:     return "ArrayLit";
        default:                return "<unknown>";
    }
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

void ast_print(const AstNode *node, int indent) {
    if (!node) { print_indent(indent); printf("(null)\n"); return; }
    print_indent(indent);
    printf("%s", ast_node_kind_str(node->kind));
    switch (node->kind) {
        case AST_PROGRAM:
            printf(" (%u decls)\n", node->program.decl_count);
            for (u32 i = 0; i < node->program.decl_count; i++)
                ast_print(node->program.decls[i], indent + 1);
            break;
        case AST_FN_DECL:
            printf(" '%.*s' (%u params)%s\n", (int)node->fn_decl.name.length,
                   node->fn_decl.name.data, node->fn_decl.param_count,
                   node->fn_decl.is_pub ? " pub" : "");
            if (node->fn_decl.body) ast_print(node->fn_decl.body, indent + 1);
            break;
        case AST_STRUCT_DECL:
            printf(" '%.*s' (%u fields)\n", (int)node->struct_decl.name.length,
                   node->struct_decl.name.data, node->struct_decl.field_count);
            break;
        case AST_DRIVER_DECL:
            printf(" '%.*s' (%u fields, %u methods)\n", (int)node->driver_decl.name.length,
                   node->driver_decl.name.data, node->driver_decl.field_count, node->driver_decl.method_count);
            for (u32 i = 0; i < node->driver_decl.method_count; i++)
                ast_print(node->driver_decl.methods[i], indent + 1);
            break;
        case AST_SYSCALL_DECL:
            printf(" '%.*s' (%u params)\n", (int)node->syscall_decl.name.length,
                   node->syscall_decl.name.data, node->syscall_decl.param_count);
            if (node->syscall_decl.body) ast_print(node->syscall_decl.body, indent + 1);
            break;
        case AST_MODULE_DECL:
            printf("\n");
            ast_print(node->module_decl.path, indent + 1);
            break;
        case AST_IMPORT_DECL:
            printf(" (%u items)\n", node->import_decl.item_count);
            ast_print(node->import_decl.path, indent + 1);
            break;
        case AST_CONST_DECL:
            printf(" '%.*s'\n", (int)node->const_decl.name.length, node->const_decl.name.data);
            break;
        case AST_BLOCK:
            printf(" (%u stmts)\n", node->block.stmt_count);
            for (u32 i = 0; i < node->block.stmt_count; i++)
                ast_print(node->block.stmts[i], indent + 1);
            break;
        case AST_LET_STMT:
            printf(" '%.*s'%s\n", (int)node->let_stmt.name.length,
                   node->let_stmt.name.data, node->let_stmt.is_mut ? " mut" : "");
            ast_print(node->let_stmt.init, indent + 1);
            break;
        case AST_RETURN_STMT:
            printf("\n");
            if (node->return_stmt.value) ast_print(node->return_stmt.value, indent + 1);
            break;
        case AST_EXPR_STMT:
            printf("\n");
            ast_print(node->expr_stmt.expr, indent + 1);
            break;
        case AST_ASSIGN_STMT:
            printf(" (op=%d)\n", node->assign_stmt.op);
            ast_print(node->assign_stmt.target, indent + 1);
            ast_print(node->assign_stmt.value, indent + 1);
            break;
        case AST_INT_LIT:  printf(" %llu\n", (unsigned long long)node->int_lit.value); break;
        case AST_FLOAT_LIT:printf(" %g\n", node->float_lit.value); break;
        case AST_STRING_LIT:printf(" \"%.*s\"\n", (int)node->string_lit.value.length, node->string_lit.value.data); break;
        case AST_BOOL_LIT: printf(" %s\n", node->bool_lit.value ? "true" : "false"); break;
        case AST_IDENT:    printf(" %.*s\n", (int)node->ident.name.length, node->ident.name.data); break;
        case AST_PATH:
            for (u32 i = 0; i < node->path.segment_count; i++) {
                if (i > 0) printf("::");
                printf("%.*s", (int)node->path.segments[i].length, node->path.segments[i].data);
            }
            printf("\n");
            break;
        case AST_BINARY_OP:
            printf(" (op=%d)\n", node->binary.op);
            ast_print(node->binary.left, indent + 1);
            ast_print(node->binary.right, indent + 1);
            break;
        case AST_UNARY_OP:
            printf(" (op=%d)\n", node->unary.op);
            ast_print(node->unary.operand, indent + 1);
            break;
        case AST_CALL:
            printf(" (%u args)\n", node->call.arg_count);
            ast_print(node->call.callee, indent + 1);
            for (u32 i = 0; i < node->call.arg_count; i++)
                ast_print(node->call.args[i], indent + 1);
            break;
        case AST_METHOD_CALL:
            printf(" .%.*s(%u args)\n", (int)node->method_call.method_name.length,
                   node->method_call.method_name.data, node->method_call.arg_count);
            ast_print(node->method_call.object, indent + 1);
            break;
        case AST_FIELD_ACCESS:
            printf(" .%.*s\n", (int)node->field_access.field_name.length, node->field_access.field_name.data);
            ast_print(node->field_access.object, indent + 1);
            break;
        case AST_IF:
            printf("\n");
            ast_print(node->if_expr.condition, indent + 1);
            ast_print(node->if_expr.then_block, indent + 1);
            if (node->if_expr.else_block) ast_print(node->if_expr.else_block, indent + 1);
            break;
        case AST_LOOP: case AST_WHILE: case AST_FOR:
            printf("\n");
            break;
        case AST_UNSAFE_BLOCK:
            printf("\n");
            ast_print(node->unsafe_block.body, indent + 1);
            break;
        default:
            printf("\n");
            break;
    }
}
