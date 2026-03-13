/*
 * Q+ Compiler (qpc) — Lexer (Implementation)
 *
 * Hand-written lexer for the Q+ language. Handles all token types:
 * keywords, identifiers, integer/float/string/char literals,
 * operators, delimiters, and comments (including nested block comments).
 */

#include "qpc/lexer.h"
#include "qpc/diagnostic.h"

/* ── Helper: character classification ────────────────────────── */

static inline bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static inline bool is_hex_digit(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline bool is_bin_digit(char c) {
    return c == '0' || c == '1';
}

static inline bool is_oct_digit(char c) {
    return c >= '0' && c <= '7';
}

static inline bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

/* ── Lexer internals ─────────────────────────────────────────── */

static inline char peek(const Lexer *lex) {
    if (lex->cursor >= lex->source->length) return '\0';
    return lex->source->content[lex->cursor];
}

static inline char peek_at(const Lexer *lex, u32 offset) {
    u32 pos = lex->cursor + offset;
    if (pos >= lex->source->length) return '\0';
    return lex->source->content[pos];
}

static inline char advance(Lexer *lex) {
    if (lex->cursor >= lex->source->length) return '\0';
    char c = lex->source->content[lex->cursor];
    lex->cursor++;
    return c;
}

static inline bool match_char(Lexer *lex, char expected) {
    if (peek(lex) == expected) {
        lex->cursor++;
        return true;
    }
    return false;
}

static inline Span make_span(u16 source_id, u32 start, u32 end) {
    Span s;
    s.start = start;
    s.end = end;
    s.source_id = source_id;
    return s;
}

static inline Token make_token(TokenKind kind, u16 source_id, u32 start, u32 end) {
    Token tok;
    memset(&tok, 0, sizeof(Token));
    tok.kind = kind;
    tok.span = make_span(source_id, start, end);
    return tok;
}

static inline Token make_error_token(Lexer *lex, u32 start, const char *msg) {
    Span span = make_span(lex->source_id, start, lex->cursor);
    diag_emit(DIAG_ERROR, lex->source_id, span, "%s", msg);
    return make_token(TOK_ERROR, lex->source_id, start, lex->cursor);
}

/* ── Skip whitespace ─────────────────────────────────────────── */
static void skip_whitespace(Lexer *lex) {
    while (lex->cursor < lex->source->length) {
        char c = peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lex->cursor++;
        } else {
            break;
        }
    }
}

/* ── Skip comments ───────────────────────────────────────────── */
/* Returns true if a comment was skipped */
static bool skip_comment(Lexer *lex) {
    if (peek(lex) != '/') return false;

    /* Single-line comment: // */
    if (peek_at(lex, 1) == '/') {
        lex->cursor += 2;
        while (lex->cursor < lex->source->length && peek(lex) != '\n') {
            lex->cursor++;
        }
        return true;
    }

    /* Block comment: slash-star ... star-slash (nestable) */
    if (peek_at(lex, 1) == '*') {
        u32 start = lex->cursor;
        lex->cursor += 2;
        int depth = 1;

        while (lex->cursor < lex->source->length && depth > 0) {
            if (peek(lex) == '/' && peek_at(lex, 1) == '*') {
                depth++;
                lex->cursor += 2;
            } else if (peek(lex) == '*' && peek_at(lex, 1) == '/') {
                depth--;
                lex->cursor += 2;
            } else {
                lex->cursor++;
            }
        }

        if (depth > 0) {
            Span span = make_span(lex->source_id, start, lex->cursor);
            diag_emit(DIAG_ERROR, lex->source_id, span,
                      "unterminated block comment");
        }
        return true;
    }

    return false;
}

/* Skip all whitespace and comments */
static void skip_trivia(Lexer *lex) {
    for (;;) {
        skip_whitespace(lex);
        if (!skip_comment(lex)) break;
    }
}

/* ── Lex identifier or keyword ───────────────────────────────── */
static Token lex_identifier(Lexer *lex) {
    u32 start = lex->cursor;

    while (lex->cursor < lex->source->length && is_alnum(peek(lex))) {
        lex->cursor++;
    }

    const char *text = lex->source->content + start;
    usize length = lex->cursor - start;

    /* Check if it's a keyword */
    TokenKind kind = keyword_lookup(text, length);

    Token tok = make_token(kind, lex->source_id, start, lex->cursor);
    if (kind == TOK_IDENTIFIER) {
        tok.ident.data = text;
        tok.ident.length = length;
    }
    return tok;
}

/* ── Lex number (integer or float) ───────────────────────────── */
static Token lex_number(Lexer *lex) {
    u32 start = lex->cursor;
    IntBase base = INT_BASE_DEC;
    u64 value = 0;
    bool is_float = false;

    /* Check for base prefix */
    if (peek(lex) == '0' && lex->cursor + 1 < lex->source->length) {
        char next = peek_at(lex, 1);

        if (next == 'x' || next == 'X') {
            base = INT_BASE_HEX;
            lex->cursor += 2;

            if (!is_hex_digit(peek(lex))) {
                return make_error_token(lex, start, "expected hex digit after '0x'");
            }

            while (is_hex_digit(peek(lex)) || peek(lex) == '_') {
                char c = advance(lex);
                if (c == '_') continue;
                u64 digit;
                if (c >= '0' && c <= '9') digit = (u64)(c - '0');
                else if (c >= 'a' && c <= 'f') digit = (u64)(c - 'a' + 10);
                else digit = (u64)(c - 'A' + 10);
                value = value * 16 + digit;
            }

            Token tok = make_token(TOK_INT_LITERAL, lex->source_id, start, lex->cursor);
            tok.int_lit.value = value;
            tok.int_lit.base = base;
            return tok;

        } else if (next == 'b' || next == 'B') {
            base = INT_BASE_BIN;
            lex->cursor += 2;

            if (!is_bin_digit(peek(lex))) {
                return make_error_token(lex, start, "expected binary digit after '0b'");
            }

            while (is_bin_digit(peek(lex)) || peek(lex) == '_') {
                char c = advance(lex);
                if (c == '_') continue;
                value = value * 2 + (u64)(c - '0');
            }

            Token tok = make_token(TOK_INT_LITERAL, lex->source_id, start, lex->cursor);
            tok.int_lit.value = value;
            tok.int_lit.base = base;
            return tok;

        } else if (next == 'o' || next == 'O') {
            base = INT_BASE_OCT;
            lex->cursor += 2;

            if (!is_oct_digit(peek(lex))) {
                return make_error_token(lex, start, "expected octal digit after '0o'");
            }

            while (is_oct_digit(peek(lex)) || peek(lex) == '_') {
                char c = advance(lex);
                if (c == '_') continue;
                value = value * 8 + (u64)(c - '0');
            }

            Token tok = make_token(TOK_INT_LITERAL, lex->source_id, start, lex->cursor);
            tok.int_lit.value = value;
            tok.int_lit.base = base;
            return tok;
        }
    }

    /* Decimal integer or float */
    while (is_digit(peek(lex)) || peek(lex) == '_') {
        char c = advance(lex);
        if (c == '_') continue;
        value = value * 10 + (u64)(c - '0');
    }

    /* Check for float: decimal point */
    if (peek(lex) == '.' && peek_at(lex, 1) != '.') {
        /* It's a float (but not a range ..) */
        is_float = true;
        lex->cursor++;  /* consume '.' */
        while (is_digit(peek(lex)) || peek(lex) == '_') {
            lex->cursor++;
        }
    }

    /* Check for float: exponent */
    if (peek(lex) == 'e' || peek(lex) == 'E') {
        is_float = true;
        lex->cursor++;
        if (peek(lex) == '+' || peek(lex) == '-') {
            lex->cursor++;
        }
        if (!is_digit(peek(lex))) {
            return make_error_token(lex, start, "expected digit in exponent");
        }
        while (is_digit(peek(lex)) || peek(lex) == '_') {
            lex->cursor++;
        }
    }

    if (is_float) {
        /* Parse the float value from the source text */
        usize len = lex->cursor - start;
        char *buf = (char *)malloc(len + 1);
        usize j = 0;
        for (usize i = 0; i < len; i++) {
            char c = lex->source->content[start + i];
            if (c != '_') buf[j++] = c;
        }
        buf[j] = '\0';
        f64 fval = strtod(buf, NULL);
        free(buf);

        Token tok = make_token(TOK_FLOAT_LITERAL, lex->source_id, start, lex->cursor);
        tok.float_lit = fval;
        return tok;
    }

    Token tok = make_token(TOK_INT_LITERAL, lex->source_id, start, lex->cursor);
    tok.int_lit.value = value;
    tok.int_lit.base = base;
    return tok;
}

/* ── Process escape sequences ────────────────────────────────── */
static u32 lex_escape(Lexer *lex) {
    char c = advance(lex);
    switch (c) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case '0':  return '\0';
        case 'x': {
            /* \xNN — two hex digits */
            if (!is_hex_digit(peek(lex)) || !is_hex_digit(peek_at(lex, 1))) {
                /* rewind and report */
                return '?';
            }
            char h = advance(lex);
            char l = advance(lex);
            u32 val = 0;
            if (h >= '0' && h <= '9') val = (u32)(h - '0');
            else if (h >= 'a' && h <= 'f') val = (u32)(h - 'a' + 10);
            else val = (u32)(h - 'A' + 10);
            val <<= 4;
            if (l >= '0' && l <= '9') val |= (u32)(l - '0');
            else if (l >= 'a' && l <= 'f') val |= (u32)(l - 'a' + 10);
            else val |= (u32)(l - 'A' + 10);
            return val;
        }
        default:
            return (u32)c;  /* Unknown escape, return as-is */
    }
}

/* ── Lex string literal ──────────────────────────────────────── */
static Token lex_string(Lexer *lex, bool is_raw) {
    u32 start = lex->cursor;
    if (is_raw) {
        lex->cursor++;  /* skip 'r' */
    }
    lex->cursor++;  /* skip opening '"' */

    /* For simplicity, the token points into the source.
     * Escape processing happens later during semantic analysis. */
    u32 content_start = lex->cursor;

    if (is_raw) {
        /* Raw string: no escape processing */
        while (lex->cursor < lex->source->length && peek(lex) != '"') {
            if (peek(lex) == '\n') {
                /* Allow multi-line strings */
            }
            lex->cursor++;
        }
    } else {
        /* Regular string: skip escape sequences */
        while (lex->cursor < lex->source->length && peek(lex) != '"') {
            if (peek(lex) == '\\') {
                lex->cursor++;  /* skip backslash */
                if (lex->cursor < lex->source->length) {
                    lex->cursor++;  /* skip escaped char */
                    /* For \xNN, skip extra chars */
                    if (lex->source->content[lex->cursor - 1] == 'x') {
                        if (is_hex_digit(peek(lex))) lex->cursor++;
                        if (is_hex_digit(peek(lex))) lex->cursor++;
                    }
                }
            } else if (peek(lex) == '\n') {
                /* Allow multi-line strings */
                lex->cursor++;
            } else {
                lex->cursor++;
            }
        }
    }

    u32 content_end = lex->cursor;

    if (peek(lex) != '"') {
        return make_error_token(lex, start, "unterminated string literal");
    }
    lex->cursor++;  /* skip closing '"' */

    TokenKind kind = is_raw ? TOK_RAW_STRING_LITERAL : TOK_STRING_LITERAL;
    Token tok = make_token(kind, lex->source_id, start, lex->cursor);
    tok.str_lit.data = lex->source->content + content_start;
    tok.str_lit.length = content_end - content_start;
    tok.str_lit.is_raw = is_raw;
    return tok;
}

/* ── Lex char literal ────────────────────────────────────────── */
static Token lex_char(Lexer *lex) {
    u32 start = lex->cursor;
    lex->cursor++;  /* skip opening '\'' */

    u32 value;
    if (peek(lex) == '\\') {
        lex->cursor++;  /* skip backslash */
        value = lex_escape(lex);
    } else if (peek(lex) == '\'') {
        return make_error_token(lex, start, "empty character literal");
    } else {
        value = (u32)(u8)advance(lex);
    }

    if (peek(lex) != '\'') {
        /* Consume until we find closing quote or newline */
        while (lex->cursor < lex->source->length &&
               peek(lex) != '\'' && peek(lex) != '\n') {
            lex->cursor++;
        }
        if (peek(lex) == '\'') lex->cursor++;
        return make_error_token(lex, start, "character literal too long or unterminated");
    }
    lex->cursor++;  /* skip closing '\'' */

    Token tok = make_token(TOK_CHAR_LITERAL, lex->source_id, start, lex->cursor);
    tok.char_lit = value;
    return tok;
}

/* ── Lex operator or delimiter ───────────────────────────────── */
static Token lex_operator(Lexer *lex) {
    u32 start = lex->cursor;
    char c = advance(lex);

    switch (c) {
        /* Single-character tokens (no ambiguity) */
        case '(': return make_token(TOK_LPAREN,    lex->source_id, start, lex->cursor);
        case ')': return make_token(TOK_RPAREN,    lex->source_id, start, lex->cursor);
        case '{': return make_token(TOK_LBRACE,    lex->source_id, start, lex->cursor);
        case '}': return make_token(TOK_RBRACE,    lex->source_id, start, lex->cursor);
        case '[': return make_token(TOK_LBRACKET,  lex->source_id, start, lex->cursor);
        case ']': return make_token(TOK_RBRACKET,  lex->source_id, start, lex->cursor);
        case ',': return make_token(TOK_COMMA,     lex->source_id, start, lex->cursor);
        case ';': return make_token(TOK_SEMICOLON, lex->source_id, start, lex->cursor);
        case '~': return make_token(TOK_TILDE,     lex->source_id, start, lex->cursor);
        case '@': return make_token(TOK_AT,        lex->source_id, start, lex->cursor);
        case '?': return make_token(TOK_QUESTION,  lex->source_id, start, lex->cursor);

        /* # — hash (for attributes) */
        case '#': return make_token(TOK_HASH, lex->source_id, start, lex->cursor);

        /* + += */
        case '+':
            if (match_char(lex, '=')) return make_token(TOK_PLUS_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_PLUS, lex->source_id, start, lex->cursor);

        /* - -= -> */
        case '-':
            if (match_char(lex, '=')) return make_token(TOK_MINUS_EQ, lex->source_id, start, lex->cursor);
            if (match_char(lex, '>')) return make_token(TOK_ARROW,    lex->source_id, start, lex->cursor);
            return make_token(TOK_MINUS, lex->source_id, start, lex->cursor);

        /* * *= */
        case '*':
            if (match_char(lex, '=')) return make_token(TOK_STAR_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_STAR, lex->source_id, start, lex->cursor);

        /* / /= (comments already handled) */
        case '/':
            if (match_char(lex, '=')) return make_token(TOK_SLASH_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_SLASH, lex->source_id, start, lex->cursor);

        /* % %= */
        case '%':
            if (match_char(lex, '=')) return make_token(TOK_PERCENT_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_PERCENT, lex->source_id, start, lex->cursor);

        /* & &= */
        case '&':
            if (match_char(lex, '=')) return make_token(TOK_AMP_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_AMP, lex->source_id, start, lex->cursor);

        /* | |= */
        case '|':
            if (match_char(lex, '=')) return make_token(TOK_PIPE_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_PIPE, lex->source_id, start, lex->cursor);

        /* ^ ^= */
        case '^':
            if (match_char(lex, '=')) return make_token(TOK_CARET_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_CARET, lex->source_id, start, lex->cursor);

        /* = == => */
        case '=':
            if (match_char(lex, '=')) return make_token(TOK_EQ_EQ,     lex->source_id, start, lex->cursor);
            if (match_char(lex, '>')) return make_token(TOK_FAT_ARROW, lex->source_id, start, lex->cursor);
            return make_token(TOK_EQ, lex->source_id, start, lex->cursor);

        /* ! != */
        case '!':
            if (match_char(lex, '=')) return make_token(TOK_NOT_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_BANG, lex->source_id, start, lex->cursor);

        /* < <= << <<= */
        case '<':
            if (match_char(lex, '<')) {
                if (match_char(lex, '=')) return make_token(TOK_SHL_EQ, lex->source_id, start, lex->cursor);
                return make_token(TOK_SHL, lex->source_id, start, lex->cursor);
            }
            if (match_char(lex, '=')) return make_token(TOK_LT_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_LT, lex->source_id, start, lex->cursor);

        /* > >= >> >>= */
        case '>':
            if (match_char(lex, '>')) {
                if (match_char(lex, '=')) return make_token(TOK_SHR_EQ, lex->source_id, start, lex->cursor);
                return make_token(TOK_SHR, lex->source_id, start, lex->cursor);
            }
            if (match_char(lex, '=')) return make_token(TOK_GT_EQ, lex->source_id, start, lex->cursor);
            return make_token(TOK_GT, lex->source_id, start, lex->cursor);

        /* : :: */
        case ':':
            if (match_char(lex, ':')) return make_token(TOK_COLON_COLON, lex->source_id, start, lex->cursor);
            return make_token(TOK_COLON, lex->source_id, start, lex->cursor);

        /* . .. ..= */
        case '.':
            if (match_char(lex, '.')) {
                if (match_char(lex, '=')) return make_token(TOK_DOT_DOT_EQ, lex->source_id, start, lex->cursor);
                return make_token(TOK_DOT_DOT, lex->source_id, start, lex->cursor);
            }
            return make_token(TOK_DOT, lex->source_id, start, lex->cursor);

        default:
            return make_error_token(lex, start, "unexpected character");
    }
}

/* ── Main lexer entry point ──────────────────────────────────── */

void lexer_init(Lexer *lex, const SourceFile *source) {
    lex->source = source;
    lex->cursor = 0;
    lex->source_id = source->id;
}

Token lexer_next_token(Lexer *lex) {
    skip_trivia(lex);

    if (lex->cursor >= lex->source->length) {
        return make_token(TOK_EOF, lex->source_id, lex->cursor, lex->cursor);
    }

    char c = peek(lex);

    /* Identifier or keyword */
    if (is_alpha(c)) {
        /* Special case: 'r' followed by '"' is a raw string */
        if (c == 'r' && peek_at(lex, 1) == '"') {
            return lex_string(lex, true);
        }
        /* Special case: 'b' followed by '\'' is a byte literal (treat as char) */
        if (c == 'b' && peek_at(lex, 1) == '\'') {
            lex->cursor++;  /* skip 'b' */
            return lex_char(lex);
        }
        return lex_identifier(lex);
    }

    /* Number literal */
    if (is_digit(c)) {
        return lex_number(lex);
    }

    /* String literal */
    if (c == '"') {
        return lex_string(lex, false);
    }

    /* Character literal */
    if (c == '\'') {
        return lex_char(lex);
    }

    /* Operator or delimiter */
    return lex_operator(lex);
}

/* ── Lex all tokens ──────────────────────────────────────────── */
Token *lexer_lex_all(Lexer *lex, u32 *out_count) {
    u32 capacity = 1024;
    u32 count = 0;
    Token *tokens = (Token *)malloc(capacity * sizeof(Token));

    for (;;) {
        if (count >= capacity) {
            capacity *= 2;
            tokens = (Token *)realloc(tokens, capacity * sizeof(Token));
        }

        Token tok = lexer_next_token(lex);
        tokens[count++] = tok;

        if (tok.kind == TOK_EOF) break;
    }

    if (out_count) *out_count = count;
    return tokens;
}

/* ── Token kind to string ────────────────────────────────────── */
const char *token_kind_to_str(TokenKind kind) {
    switch (kind) {
        case TOK_EOF:               return "EOF";
        case TOK_ERROR:             return "ERROR";
        case TOK_INT_LITERAL:       return "INT";
        case TOK_FLOAT_LITERAL:     return "FLOAT";
        case TOK_STRING_LITERAL:    return "STRING";
        case TOK_RAW_STRING_LITERAL:return "RAW_STRING";
        case TOK_CHAR_LITERAL:      return "CHAR";
        case TOK_IDENTIFIER:        return "IDENT";

        /* Keywords */
        case TOK_KW_U8:        return "u8";
        case TOK_KW_U16:       return "u16";
        case TOK_KW_U32:       return "u32";
        case TOK_KW_U64:       return "u64";
        case TOK_KW_I8:        return "i8";
        case TOK_KW_I16:       return "i16";
        case TOK_KW_I32:       return "i32";
        case TOK_KW_I64:       return "i64";
        case TOK_KW_F32:       return "f32";
        case TOK_KW_F64:       return "f64";
        case TOK_KW_BOOL:      return "bool";
        case TOK_KW_CHAR:      return "char";
        case TOK_KW_STR:       return "str";
        case TOK_KW_VOID:      return "void";
        case TOK_KW_USIZE:     return "usize";
        case TOK_KW_ISIZE:     return "isize";
        case TOK_KW_FN:        return "fn";
        case TOK_KW_LET:       return "let";
        case TOK_KW_CONST:     return "const";
        case TOK_KW_MUT:       return "mut";
        case TOK_KW_STRUCT:    return "struct";
        case TOK_KW_ENUM:      return "enum";
        case TOK_KW_UNION:     return "union";
        case TOK_KW_TYPE:      return "type";
        case TOK_KW_ALIAS:     return "alias";
        case TOK_KW_PUB:       return "pub";
        case TOK_KW_PRIV:      return "priv";
        case TOK_KW_MODULE:    return "module";
        case TOK_KW_IMPORT:    return "import";
        case TOK_KW_STATIC:    return "static";
        case TOK_KW_EXTERN:    return "extern";
        case TOK_KW_IMPL:      return "impl";
        case TOK_KW_TRAIT:     return "trait";
        case TOK_KW_IF:        return "if";
        case TOK_KW_ELSE:      return "else";
        case TOK_KW_MATCH:     return "match";
        case TOK_KW_FOR:       return "for";
        case TOK_KW_WHILE:     return "while";
        case TOK_KW_LOOP:      return "loop";
        case TOK_KW_BREAK:     return "break";
        case TOK_KW_CONTINUE:  return "continue";
        case TOK_KW_RETURN:    return "return";
        case TOK_KW_DEFER:     return "defer";
        case TOK_KW_SAFE:      return "safe";
        case TOK_KW_UNSAFE:    return "unsafe";
        case TOK_KW_ASM:       return "asm";
        case TOK_KW_VOLATILE:  return "volatile";
        case TOK_KW_MMIO:      return "mmio";
        case TOK_KW_PORT:      return "port";
        case TOK_KW_ISR:       return "isr";
        case TOK_KW_DRIVER:    return "driver";
        case TOK_KW_SYSCALL:   return "syscall";
        case TOK_KW_INTERRUPT: return "interrupt";
        case TOK_KW_KERNEL:    return "kernel";
        case TOK_KW_USERLAND:  return "userland";
        case TOK_KW_ALLOC:     return "alloc";
        case TOK_KW_FREE:      return "free";
        case TOK_KW_REF:       return "ref";
        case TOK_KW_PTR:       return "ptr";
        case TOK_KW_OWN:       return "own";
        case TOK_KW_SLICE:     return "slice";
        case TOK_KW_AS:        return "as";
        case TOK_KW_IN:        return "in";
        case TOK_KW_IS:        return "is";
        case TOK_KW_NOT:       return "not";
        case TOK_KW_AND:       return "and";
        case TOK_KW_OR:        return "or";
        case TOK_KW_TRUE:      return "true";
        case TOK_KW_FALSE:     return "false";
        case TOK_KW_NULL:      return "null";
        case TOK_KW_SELF:      return "self";
        case TOK_KW_SELF_TYPE: return "Self";

        /* Operators */
        case TOK_PLUS:       return "+";
        case TOK_MINUS:      return "-";
        case TOK_STAR:       return "*";
        case TOK_SLASH:      return "/";
        case TOK_PERCENT:    return "%";
        case TOK_AMP:        return "&";
        case TOK_PIPE:       return "|";
        case TOK_CARET:      return "^";
        case TOK_TILDE:      return "~";
        case TOK_SHL:        return "<<";
        case TOK_SHR:        return ">>";
        case TOK_EQ_EQ:      return "==";
        case TOK_NOT_EQ:     return "!=";
        case TOK_LT:         return "<";
        case TOK_GT:         return ">";
        case TOK_LT_EQ:      return "<=";
        case TOK_GT_EQ:      return ">=";
        case TOK_EQ:         return "=";
        case TOK_PLUS_EQ:    return "+=";
        case TOK_MINUS_EQ:   return "-=";
        case TOK_STAR_EQ:    return "*=";
        case TOK_SLASH_EQ:   return "/=";
        case TOK_PERCENT_EQ: return "%=";
        case TOK_AMP_EQ:     return "&=";
        case TOK_PIPE_EQ:    return "|=";
        case TOK_CARET_EQ:   return "^=";
        case TOK_SHL_EQ:     return "<<=";
        case TOK_SHR_EQ:     return ">>=";
        case TOK_ARROW:      return "->";
        case TOK_FAT_ARROW:  return "=>";
        case TOK_DOT_DOT:    return "..";
        case TOK_DOT_DOT_EQ: return "..=";
        case TOK_COLON_COLON:return "::";
        case TOK_QUESTION:   return "?";
        case TOK_AT:         return "@";
        case TOK_BANG:       return "!";

        /* Delimiters */
        case TOK_LPAREN:     return "(";
        case TOK_RPAREN:     return ")";
        case TOK_LBRACE:     return "{";
        case TOK_RBRACE:     return "}";
        case TOK_LBRACKET:   return "[";
        case TOK_RBRACKET:   return "]";
        case TOK_COMMA:      return ",";
        case TOK_COLON:      return ":";
        case TOK_SEMICOLON:  return ";";
        case TOK_DOT:        return ".";
        case TOK_HASH:       return "#";

        default:             return "<unknown>";
    }
}

/* ── Token debug print ───────────────────────────────────────── */
void token_print(const Token *tok, const char *source) {
    SourceLoc loc = {0, 0};

    printf("%-16s", token_kind_to_str(tok->kind));

    /* Print extra info for literals and identifiers */
    switch (tok->kind) {
        case TOK_INT_LITERAL:
            printf(" value=%llu base=%d", (unsigned long long)tok->int_lit.value,
                   (int)tok->int_lit.base);
            break;
        case TOK_FLOAT_LITERAL:
            printf(" value=%g", tok->float_lit);
            break;
        case TOK_STRING_LITERAL:
        case TOK_RAW_STRING_LITERAL:
            printf(" \"%.*s\"", (int)tok->str_lit.length, tok->str_lit.data);
            break;
        case TOK_CHAR_LITERAL:
            if (tok->char_lit >= 32 && tok->char_lit < 127)
                printf(" '%c'", (char)tok->char_lit);
            else
                printf(" '\\x%02X'", tok->char_lit);
            break;
        case TOK_IDENTIFIER:
            printf(" %.*s", (int)tok->ident.length, tok->ident.data);
            break;
        default:
            break;
    }

    /* Print span */
    printf(" [%u..%u]", tok->span.start, tok->span.end);
    printf("\n");

    (void)source;
    (void)loc;
}
