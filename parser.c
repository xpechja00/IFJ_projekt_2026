/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   parser.c - syntaktický analyzátor (rekurzivní sestup)
 *
 * Autor: xdundrm00
 */

#include "parser.h"
#include "scanner.h"
#include "error.h"
#include "symtable.h"
#include <string.h>

// Aktuálně zpracovávaný token
static tToken current_token;

// Deklarace interních funkcí pro rekurzivní sestup
int parse_program();
int parse_statement();
int parse_expression();
int parse_list();
int parse_control_flow();
int parse_function_expression();

// Hlavní funkce
int parse() {
    current_token = scanner_next();
    return parse_program();
}

int parse_program() {
    // povinný prolog: import ifj26.*
    if (current_token.type != TOK_KW_IMPORT) 
        error_exit(ERR_SYNTAX, "Očekáváno klíčové slovo 'import'");
    current_token = scanner_next();
    
    if (current_token.type != TOK_IDENT || strcmp(current_token.value.str_val, "ifj26") != 0) 
        error_exit(ERR_SYNTAX, "Očekáván identifikátor 'ifj26'");
    current_token = scanner_next();
    
    if (current_token.type != TOK_DOT) 
        error_exit(ERR_SYNTAX, "Očekávána tečka za 'ifj26'");
    current_token = scanner_next();
    
    if (current_token.type != TOK_STAR) 
        error_exit(ERR_SYNTAX, "Očekávána hvězdička v prologu");
    current_token = scanner_next();
    
    // Hlavní cyklus zpracování na nejvyšší úrovni
    while (current_token.type != TOK_EOF) {
        if (current_token.type == TOK_EOL) {
            current_token = scanner_next();
            continue;
        }

        if (current_token.type == TOK_KW_DEF) {
            parse_function_expression();
        } else if (current_token.type == TOK_KW_VAR || current_token.type == TOK_KW_VAL) {
            parse_statement();
        } else {
            error_exit(ERR_SYNTAX, "Neočekávaný token na nejvyšší úrovni programu");
        }
    }
    
    return ERR_OK; 
}

// Příkazy a struktury
int parse_statement() {
    current_token = scanner_next();
    return ERR_OK;
}

// Výrazy (precedenční analýza)
int parse_expression() {
    return ERR_OK;
}

// Rozšíření
int parse_list() {
    return ERR_OK;
}

int parse_control_flow() {
    return ERR_OK;
}

int parse_function_expression() {
    // 1. Def
    current_token = scanner_next();

    // 2. Název funkce
    if (current_token.type != TOK_IDENT) {
        error_exit(ERR_SYNTAX, "Očekáván název funkce po 'def'");
    }
    // TODO: Uložení názvu do tabulky symbolů
    current_token = scanner_next();

    // 3. Levá závorka
    if (current_token.type != TOK_LPAREN) {
        error_exit(ERR_SYNTAX, "Očekávána '(' za názvem funkce");
    }
    current_token = scanner_next();

    // 4. Parametry (kontrola 'identifikátor : Typ')
    if (current_token.type != TOK_RPAREN) {
        while (true) {
            // Název parametru
            if (current_token.type != TOK_IDENT) {
                error_exit(ERR_SYNTAX, "Očekáván název parametru");
            }
            current_token = scanner_next();

            // Dvojtečka
            if (current_token.type != TOK_COLON) {
                error_exit(ERR_SYNTAX, "Očekávána ':' za názvem parametru");
            }
            current_token = scanner_next();

            // Datový typ parametru
            if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
                current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
                current_token.type != TOK_KW_BOOLEAN) {
                error_exit(ERR_SYNTAX, "Očekáván datový typ parametru (Int, Double, String, Unit, Boolean)");
            }
            current_token = scanner_next();

            // Čárka pro další parametr, nebo konec závorky
            if (current_token.type == TOK_COMMA) {
                current_token = scanner_next(); 
            } else if (current_token.type == TOK_RPAREN) {
                break; 
            } else {
                error_exit(ERR_SYNTAX, "Očekávána ',' nebo ')' v parametrech funkce");
            }
        }
    }
    
    current_token = scanner_next();

    // 5. Návratový typ (očekáváme ':' a typ)
    if (current_token.type != TOK_COLON) {
        error_exit(ERR_SYNTAX, "Očekávána ':' a návratový typ funkce");
    }
    current_token = scanner_next();

    if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
        current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
        current_token.type != TOK_KW_BOOLEAN) {
        error_exit(ERR_SYNTAX, "Očekáván platný datový typ (Int, Double, String, Unit, Boolean)");
    }
    current_token = scanner_next();

    // 6. Znak přiřazení '='
    if (current_token.type != TOK_ASSIGN) {
        error_exit(ERR_SYNTAX, "Očekáváno '=' před tělem funkce");
    }
    current_token = scanner_next();

    // TODO: Kontrola bloku kodu

    return ERR_OK;
}