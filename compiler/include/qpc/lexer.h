/*
 * Q+ Compiler (qpc) — Lexer
 *
 * Tokenizes Q+ source code into a stream of typed tokens.
 */

#ifndef QPC_LEXER_H
#define QPC_LEXER_H

#include "common.h"
#include "token.h"
#include "source.h"

typedef struct Lexer {
    const SourceFile *source;
    u32 cursor;     /* Current byte offset into source content */
    u16 source_id;  /* Source file ID for spans */
} Lexer;

/* Initialize a lexer for a source file */
void lexer_init(Lexer *lex, const SourceFile *source);

/* Get the next token. Returns TOK_EOF at end of input. */
Token lexer_next_token(Lexer *lex);

/* Lex an entire source file and return an array of tokens.
 * The returned array is heap-allocated and null-terminated (last token is TOK_EOF).
 * Caller must free() the returned array. */
Token *lexer_lex_all(Lexer *lex, u32 *out_count);

#endif /* QPC_LEXER_H */
