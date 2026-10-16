/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   scanner.h - rozhraní lexikálního analyzátoru
 *
 * Autor: xpechja00
 */

#ifndef SCANNER_H
#define SCANNER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TOK_EOF,
    TOK_EOL,
    TOK_INDENT,
    TOK_DEDENT,

    // literály a identifikátor
    TOK_IDENT,
    TOK_INT_LIT,
    TOK_DOUBLE_LIT,
    TOK_STRING_LIT,
    TOK_TRUE_LIT,
    TOK_FALSE_LIT,

    // klíčová slova
    TOK_KW_DEF,
    TOK_KW_DO,
    TOK_KW_ELSE,
    TOK_KW_IF,
    TOK_KW_IMPORT,
    TOK_KW_RETURN,
    TOK_KW_THEN,
    TOK_KW_VAL,
    TOK_KW_VAR,
    TOK_KW_WHILE,
    TOK_KW_FOR,
    TOK_KW_CASE, // rezervované, neimplementujeme
    TOK_KW_CLASS, // rezervované, neimplementujeme
    TOK_KW_EXTENSION, // rezervované, neimplementujeme
    TOK_KW_MATCH, // rezervované, neimplementujeme
    TOK_KW_TYPE, // rezervované, neimplementujeme

    // typy
    TOK_KW_INT,
    TOK_KW_DOUBLE,
    TOK_KW_STRING,
    TOK_KW_UNIT,
    TOK_KW_BOOLEAN,
    TOK_KW_LIST,

    // operátory
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_EQ, // ==
    TOK_NEQ, // !=
    TOK_ASSIGN, // =
    TOK_NOT, // !
    TOK_AND, // &&
    TOK_OR, // ||
    TOK_CONCAT, // ++ (spojení seznamů)
    TOK_ARROW_L, // <-

    // oddělovače
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_COLON,
    TOK_SEMICOLON,
    TOK_DOT,
    TOK_ANNOT_MAIN // @main
} tTokenType;

typedef struct {
    tTokenType type;
    int line; // řádek, kde token začíná
    bool nl_before; // před tokenem byl konec řádku (i uvnitř závorek)
    union {
        int32_t int_val;
        double double_val;
        char* str_val; // u TOK_IDENT a TOK_STRING_LIT, uvolnit přes token_free
    } value;
} tToken;

// Vrátí další token ze stdin. Při lexikální chybě ukončí program (chyba 1).
tToken scanner_next(void);

// Uvolní řetězec v tokenu (jen u TOK_IDENT a TOK_STRING_LIT).
void token_free(tToken* token);

#endif // SCANNER_H
