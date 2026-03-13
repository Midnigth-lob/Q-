/*
 * Q+ Compiler (qpc) — Token Definitions
 *
 * Defines all token kinds for the Q+ language, the Token struct,
 * and the keyword lookup table.
 */

#ifndef QPC_TOKEN_H
#define QPC_TOKEN_H

#include "common.h"

/* ── Source Span ──────────────────────────────────────────────── */

typedef struct Span {
    u32 start;      /* byte offset into source */
    u32 end;        /* byte offset (exclusive) */
    u16 source_id;  /* index into source file array */
} Span;

/* ── Token Kinds ─────────────────────────────────────────────── */

typedef enum TokenKind {
    /* ── Special ───────────────────────────────────────────── */
    TOK_EOF = 0,
    TOK_ERROR,

    /* ── Literals ──────────────────────────────────────────── */
    TOK_INT_LITERAL,        /* 42, 0xFF, 0b1010, 0o77 */
    TOK_FLOAT_LITERAL,      /* 3.14, 1.0e-10 */
    TOK_STRING_LITERAL,     /* "hello" */
    TOK_RAW_STRING_LITERAL, /* r"hello" */
    TOK_CHAR_LITERAL,       /* 'A' */
    TOK_IDENTIFIER,         /* my_var, PageTable */

    /* ── Keywords: Types ───────────────────────────────────── */
    TOK_KW_U8,
    TOK_KW_U16,
    TOK_KW_U32,
    TOK_KW_U64,
    TOK_KW_I8,
    TOK_KW_I16,
    TOK_KW_I32,
    TOK_KW_I64,
    TOK_KW_F32,
    TOK_KW_F64,
    TOK_KW_BOOL,
    TOK_KW_CHAR,
    TOK_KW_STR,
    TOK_KW_VOID,
    TOK_KW_USIZE,
    TOK_KW_ISIZE,

    /* ── Keywords: Declarations ────────────────────────────── */
    TOK_KW_FN,
    TOK_KW_LET,
    TOK_KW_CONST,
    TOK_KW_MUT,
    TOK_KW_STRUCT,
    TOK_KW_ENUM,
    TOK_KW_UNION,
    TOK_KW_TYPE,
    TOK_KW_ALIAS,
    TOK_KW_PUB,
    TOK_KW_PRIV,
    TOK_KW_MODULE,
    TOK_KW_IMPORT,
    TOK_KW_STATIC,
    TOK_KW_EXTERN,
    TOK_KW_IMPL,
    TOK_KW_TRAIT,

    /* ── Keywords: Control Flow ────────────────────────────── */
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_MATCH,
    TOK_KW_FOR,
    TOK_KW_WHILE,
    TOK_KW_LOOP,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_RETURN,
    TOK_KW_DEFER,

    /* ── Keywords: Safety & Low-level ──────────────────────── */
    TOK_KW_SAFE,
    TOK_KW_UNSAFE,
    TOK_KW_ASM,
    TOK_KW_VOLATILE,
    TOK_KW_MMIO,
    TOK_KW_PORT,
    TOK_KW_ISR,

    /* ── Keywords: OS Constructs ───────────────────────────── */
    TOK_KW_DRIVER,
    TOK_KW_SYSCALL,
    TOK_KW_INTERRUPT,
    TOK_KW_KERNEL,
    TOK_KW_USERLAND,

    /* ── Keywords: Memory ──────────────────────────────────── */
    TOK_KW_ALLOC,
    TOK_KW_FREE,
    TOK_KW_REF,
    TOK_KW_PTR,
    TOK_KW_OWN,
    TOK_KW_SLICE,

    /* ── Keywords: Other ───────────────────────────────────── */
    TOK_KW_AS,
    TOK_KW_IN,
    TOK_KW_IS,
    TOK_KW_NOT,
    TOK_KW_AND,
    TOK_KW_OR,
    TOK_KW_TRUE,
    TOK_KW_FALSE,
    TOK_KW_NULL,
    TOK_KW_SELF,
    TOK_KW_SELF_TYPE,   /* Self (capital S) */

    /* ── Operators: Arithmetic ─────────────────────────────── */
    TOK_PLUS,           /* + */
    TOK_MINUS,          /* - */
    TOK_STAR,           /* * */
    TOK_SLASH,          /* / */
    TOK_PERCENT,        /* % */

    /* ── Operators: Bitwise ────────────────────────────────── */
    TOK_AMP,            /* & */
    TOK_PIPE,           /* | */
    TOK_CARET,          /* ^ */
    TOK_TILDE,          /* ~ */
    TOK_SHL,            /* << */
    TOK_SHR,            /* >> */

    /* ── Operators: Comparison ─────────────────────────────── */
    TOK_EQ_EQ,          /* == */
    TOK_NOT_EQ,         /* != */
    TOK_LT,             /* < */
    TOK_GT,             /* > */
    TOK_LT_EQ,          /* <= */
    TOK_GT_EQ,          /* >= */

    /* ── Operators: Assignment ─────────────────────────────── */
    TOK_EQ,             /* = */
    TOK_PLUS_EQ,        /* += */
    TOK_MINUS_EQ,       /* -= */
    TOK_STAR_EQ,        /* *= */
    TOK_SLASH_EQ,       /* /= */
    TOK_PERCENT_EQ,     /* %= */
    TOK_AMP_EQ,         /* &= */
    TOK_PIPE_EQ,        /* |= */
    TOK_CARET_EQ,       /* ^= */
    TOK_SHL_EQ,         /* <<= */
    TOK_SHR_EQ,         /* >>= */

    /* ── Operators: Other ──────────────────────────────────── */
    TOK_ARROW,          /* -> */
    TOK_FAT_ARROW,      /* => */
    TOK_DOT_DOT,        /* .. */
    TOK_DOT_DOT_EQ,     /* ..= */
    TOK_COLON_COLON,    /* :: */
    TOK_QUESTION,       /* ? */
    TOK_AT,             /* @ */
    TOK_BANG,           /* ! */

    /* ── Delimiters ────────────────────────────────────────── */
    TOK_LPAREN,         /* ( */
    TOK_RPAREN,         /* ) */
    TOK_LBRACE,         /* { */
    TOK_RBRACE,         /* } */
    TOK_LBRACKET,       /* [ */
    TOK_RBRACKET,       /* ] */
    TOK_COMMA,          /* , */
    TOK_COLON,          /* : */
    TOK_SEMICOLON,      /* ; */
    TOK_DOT,            /* . */
    TOK_HASH,           /* # */

    /* ── Sentinel ──────────────────────────────────────────── */
    TOK_COUNT
} TokenKind;

/* ── Integer literal base ────────────────────────────────────── */
typedef enum IntBase {
    INT_BASE_DEC = 10,
    INT_BASE_HEX = 16,
    INT_BASE_BIN = 2,
    INT_BASE_OCT = 8,
} IntBase;

/* ── Token struct ────────────────────────────────────────────── */

typedef struct Token {
    TokenKind kind;
    Span      span;

    /* Literal values (valid depending on kind) */
    union {
        struct {
            u64     value;
            IntBase base;
        } int_lit;

        f64 float_lit;

        struct {
            const char *data;   /* Points into source or arena-allocated */
            usize       length;
            bool        is_raw;
        } str_lit;

        u32 char_lit;  /* Unicode codepoint */

        struct {
            const char *data;
            usize       length;
        } ident;
    };
} Token;

/* ── Keyword table entry ─────────────────────────────────────── */

typedef struct KeywordEntry {
    const char *text;
    TokenKind   kind;
} KeywordEntry;

/*
 * Sorted keyword table (for binary search).
 * Must remain sorted alphabetically!
 */
static const KeywordEntry KEYWORDS[] = {
    { "Self",      TOK_KW_SELF_TYPE },
    { "alloc",     TOK_KW_ALLOC },
    { "alias",     TOK_KW_ALIAS },
    { "and",       TOK_KW_AND },
    { "as",        TOK_KW_AS },
    { "asm",       TOK_KW_ASM },
    { "bool",      TOK_KW_BOOL },
    { "break",     TOK_KW_BREAK },
    { "char",      TOK_KW_CHAR },
    { "const",     TOK_KW_CONST },
    { "continue",  TOK_KW_CONTINUE },
    { "defer",     TOK_KW_DEFER },
    { "driver",    TOK_KW_DRIVER },
    { "else",      TOK_KW_ELSE },
    { "enum",      TOK_KW_ENUM },
    { "extern",    TOK_KW_EXTERN },
    { "f32",       TOK_KW_F32 },
    { "f64",       TOK_KW_F64 },
    { "false",     TOK_KW_FALSE },
    { "fn",        TOK_KW_FN },
    { "for",       TOK_KW_FOR },
    { "free",      TOK_KW_FREE },
    { "i16",       TOK_KW_I16 },
    { "i32",       TOK_KW_I32 },
    { "i64",       TOK_KW_I64 },
    { "i8",        TOK_KW_I8 },
    { "if",        TOK_KW_IF },
    { "impl",      TOK_KW_IMPL },
    { "import",    TOK_KW_IMPORT },
    { "in",        TOK_KW_IN },
    { "interrupt", TOK_KW_INTERRUPT },
    { "is",        TOK_KW_IS },
    { "isize",     TOK_KW_ISIZE },
    { "isr",       TOK_KW_ISR },
    { "kernel",    TOK_KW_KERNEL },
    { "let",       TOK_KW_LET },
    { "loop",      TOK_KW_LOOP },
    { "match",     TOK_KW_MATCH },
    { "mmio",      TOK_KW_MMIO },
    { "module",    TOK_KW_MODULE },
    { "mut",       TOK_KW_MUT },
    { "not",       TOK_KW_NOT },
    { "null",      TOK_KW_NULL },
    { "or",        TOK_KW_OR },
    { "own",       TOK_KW_OWN },
    { "port",      TOK_KW_PORT },
    { "priv",      TOK_KW_PRIV },
    { "ptr",       TOK_KW_PTR },
    { "pub",       TOK_KW_PUB },
    { "ref",       TOK_KW_REF },
    { "return",    TOK_KW_RETURN },
    { "safe",      TOK_KW_SAFE },
    { "self",      TOK_KW_SELF },
    { "slice",     TOK_KW_SLICE },
    { "static",    TOK_KW_STATIC },
    { "str",       TOK_KW_STR },
    { "struct",    TOK_KW_STRUCT },
    { "syscall",   TOK_KW_SYSCALL },
    { "trait",     TOK_KW_TRAIT },
    { "true",      TOK_KW_TRUE },
    { "type",      TOK_KW_TYPE },
    { "u16",       TOK_KW_U16 },
    { "u32",       TOK_KW_U32 },
    { "u64",       TOK_KW_U64 },
    { "u8",        TOK_KW_U8 },
    { "union",     TOK_KW_UNION },
    { "unsafe",    TOK_KW_UNSAFE },
    { "userland",  TOK_KW_USERLAND },
    { "usize",     TOK_KW_USIZE },
    { "void",      TOK_KW_VOID },
    { "volatile",  TOK_KW_VOLATILE },
    { "while",     TOK_KW_WHILE },
};

#define KEYWORD_COUNT ARRAY_LEN(KEYWORDS)

/* ── Lookup keyword by name (binary search) ──────────────────── */
static inline TokenKind keyword_lookup(const char *name, usize length) {
    int lo = 0;
    int hi = (int)KEYWORD_COUNT - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int cmp = strncmp(name, KEYWORDS[mid].text, length);

        if (cmp == 0) {
            /* Also check that lengths match */
            usize kw_len = strlen(KEYWORDS[mid].text);
            if (length == kw_len) {
                return KEYWORDS[mid].kind;
            } else if (length < kw_len) {
                hi = mid - 1;
            } else {
                lo = mid + 1;
            }
        } else if (cmp < 0) {
            hi = mid - 1;
        } else {
            lo = mid + 1;
        }
    }

    return TOK_IDENTIFIER;  /* Not a keyword */
}

/* ── Token kind to string ────────────────────────────────────── */
const char *token_kind_to_str(TokenKind kind);

/* Token display: human-readable representation */
void token_print(const Token *tok, const char *source);

#endif /* QPC_TOKEN_H */
