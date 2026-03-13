/*
 * Q+ Compiler (qpc) — Parser
 *
 * Recursive descent parser that transforms a token stream
 * into a typed Abstract Syntax Tree (AST).
 */

#ifndef QPC_PARSER_H
#define QPC_PARSER_H

#include "common.h"
#include "token.h"
#include "lexer.h"
#include "ast.h"
#include "source.h"

typedef struct Parser {
    Lexer      *lexer;
    Token       current;
    Token       previous;
    Arena      *arena;        /* Arena for AST allocation */
    u16         source_id;
    bool        had_error;
    bool        panic_mode;   /* Suppress cascading errors */
} Parser;

/* Initialize the parser */
void parser_init(Parser *p, Lexer *lexer, Arena *arena);

/* Parse an entire Q+ source file → AST_PROGRAM node */
AstNode *parser_parse(Parser *p);

/* Did parsing encounter any errors? */
bool parser_had_error(const Parser *p);

#endif /* QPC_PARSER_H */
