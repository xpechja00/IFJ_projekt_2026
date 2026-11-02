/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   pasrer.c - syntaktický analyzátor
 *
 * Autor: xdundrm00
 *
 */
#include "parser.h"
#include "scanner.h"
#include "error.h"
#include "symtable.h"
#include "expression.h"
#include <string.h>
#include <stdlib.h> 

static tToken current_token;
static tToken lookahead_token;  
static bool has_lookahead = false; 

// Načtení dalšího tokenu (spřípravou na lookahead)
void get_next_token() {
    if (has_lookahead) {
        current_token = lookahead_token;
        has_lookahead = false;
    } else {
        current_token = scanner_next();
    }
}

// Hlavní funkce
int parse(tSymTable* global_symtable){
    get_next_token();
    if (parse_prolog() != ERR_OK){
        return ERR_SYNTAX;
    }
    while (current_token.type != TOK_EOF){   
        if (current_token.type == TOK_EOL) {
            get_next_token();
        }

        if (current_token.type == TOK_ANNOT_MAIN) {
            get_next_token(); 
        }
        
        if (current_token.type == TOK_KW_DEF) {
            if (parse_function(global_symtable) != ERR_OK) {
                return ERR_SYNTAX;
            }
        } 
        else {
            return ERR_SYNTAX;
        }
    }
        return ERR_OK;
    } 

// Prolog: import ifj26.*
int parse_prolog(){
    // import
    if (current_token.type != TOK_KW_IMPORT) {
        return ERR_SYNTAX; 
    }
    get_next_token();

    // identifikátor ifj26
    if (current_token.type != TOK_IDENT || current_token.value.str_val == NULL || strcmp(current_token.value.str_val, "ifj26") != 0) {
        return ERR_SYNTAX;
    }
    get_next_token();

    //.
    if (current_token.type != TOK_DOT) {
        return ERR_SYNTAX;
    }
    get_next_token();

    //*
    if (current_token.type != TOK_STAR) {
        return ERR_SYNTAX;
    }
    get_next_token();

    return ERR_OK;
}

// Funkce def Nazev(id:param, id:param..)
int parse_function(tSymTable* global_symtable){
    // Def
    if (current_token.type != TOK_KW_DEF) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // Název funkce
    if (current_token.type != TOK_IDENT) {
        return ERR_SYNTAX;
    }

    // Uložení názvu
    char* func_name = current_token.value.str_val;
    if (func_name == NULL) {
        return ERR_INTERNAL;
    }
    get_next_token();

    // Příprava dat
    tData data;
    data.kind = SYM_FUNC;
    data.attribute.func.paramCount = 0;
    data.attribute.func.params = NULL;
    data.attribute.func.isDefined = true;
    data.attribute.func.returnType.type = TYPE_UNIT; 

    // Vložení do tabulky symbolů
    tInsertResult result = symtable_insert(global_symtable, func_name, data);
    if (result == INSERT_ERROR) {
        return ERR_INTERNAL; 
    }
    else if (result == INSERT_DUPLICATE){
        return ERR_REDEF;
    }

    // Parametry
    // (
    if (current_token.type != TOK_LPAREN){
        return ERR_SYNTAX;
    }
    get_next_token();
    
    // param)
    while (current_token.type != TOK_RPAREN){
        // identifikátor parametru
        if (current_token.type != TOK_IDENT) {
            return ERR_SYNTAX;
        }
        get_next_token();

        //:
        if (current_token.type != TOK_COLON) { 
            return ERR_SYNTAX;
        }
        get_next_token();

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            return ERR_SYNTAX;
        }
        get_next_token();
        
        //,
        if (current_token.type == TOK_COMMA) {
            get_next_token(); 
        } 

        //)
        else if (current_token.type == TOK_RPAREN) {
            break; 
        }

        else{
            return ERR_SYNTAX;
        }
        
    }
    get_next_token();

    // Volitelný návratový typ
    if (current_token.type == TOK_COLON) {
        get_next_token(); 

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            return ERR_SYNTAX;
        }
        get_next_token(); 
    }

    //=
    if (current_token.type != TOK_ASSIGN) { 
        return ERR_SYNTAX;
    }
    get_next_token();
    
    // Tělo funkce
    if (current_token.type == TOK_EOL) {
        get_next_token(); 

        // Začátek bloku
        if (current_token.type != TOK_INDENT) {
            return ERR_SYNTAX;
        }
        get_next_token(); 

        // Příkazy bloku
        while (current_token.type != TOK_DEDENT && current_token.type != TOK_EOF) {
            int err = parse_statement(); 
            if (err != ERR_OK) {
                return err;
            }

            if (current_token.type == TOK_EOL) {
                get_next_token();
            } else if (current_token.type != TOK_DEDENT) {
                return ERR_SYNTAX; 
            }
        }

        // Konec bloku
        if (current_token.type != TOK_DEDENT) {
            return ERR_SYNTAX;
        }
        get_next_token(); 

    } 
    else {
        // Jednořádkové tělo 
        if (parse_expression() != ERR_OK) {
            return ERR_SYNTAX;
        }
    }

    return ERR_OK; 
}

int parse_expression() {
    return parse_expression_psa();
}

// Naleznutí dalšího tokenu bez spotřebování současného
tToken parser_peek_token() {
    if (!has_lookahead) {
        lookahead_token = scanner_next();
        has_lookahead = true;
    }
    return lookahead_token;
}


// Var/Val
int parse_var() {
    get_next_token();

    // Identifikátor
    if (current_token.type != TOK_IDENT) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // :
    if (current_token.type == TOK_COLON) {
        get_next_token();

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_BOOLEAN) {
            return ERR_SYNTAX;
        }
        get_next_token();
    }

    // =
    if (current_token.type != TOK_ASSIGN) {
        return ERR_SYNTAX;
    }
    get_next_token();
    return parse_expression();
}

// Return
int parse_return() {
    get_next_token(); 

    if (current_token.type == TOK_EOL || current_token.type == TOK_DEDENT) {
        return ERR_OK; 
    }

    return parse_expression();
}

// Příkazy
int parse_statement() {
    // Var nebo val
    if (current_token.type == TOK_KW_VAL || current_token.type == TOK_KW_VAR) {
        return parse_var();
    }

    // Return
    if (current_token.type == TOK_KW_RETURN) {
        return parse_return();
    }

    // If 
    if (current_token.type == TOK_KW_IF) {
        return parse_if();
    }

    // While 
    if (current_token.type == TOK_KW_WHILE) {
        return parse_while();
    }

    // Přiřazení do proměnné
    if (current_token.type == TOK_IDENT) {
        // Zjištění dalšího tokenu
        tToken next = parser_peek_token(); 
        
        if (next.type == TOK_ASSIGN) {
            // Posuny za identifikátor a =
            get_next_token(); 
            get_next_token(); 
            return parse_expression(); 
        }
    }

    // Čistý výraz
    return parse_expression();
}

// Blok příkazů v if/while
int parse_block() {
    if (current_token.type == TOK_EOL) {
        get_next_token(); 

        if (current_token.type != TOK_INDENT) {
            return ERR_SYNTAX;
        }
        get_next_token(); 

        while (current_token.type != TOK_DEDENT && current_token.type != TOK_EOF) {
            int err = parse_statement(); 
            if (err != ERR_OK) {
                return err;
            }

            if (current_token.type == TOK_EOL) {
                get_next_token();
            } else if (current_token.type != TOK_DEDENT) {
                return ERR_SYNTAX; 
            }
        }

        if (current_token.type != TOK_DEDENT) {
            return ERR_SYNTAX;
        }
        get_next_token(); 
    } else {
        // Jednořádkový příkaz
        if (parse_statement() != ERR_OK) {
            return ERR_SYNTAX;
        }
    }
    return ERR_OK;
}

// If
int parse_if() {
    get_next_token(); 

    // Podmínka
    if (parse_expression() != ERR_OK) {
        return ERR_SYNTAX;
    }

    // Then
    if (current_token.type != TOK_KW_THEN) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // Tělo then
    if (parse_block() != ERR_OK) {
        return ERR_SYNTAX;
    }

    // Volitelné else
    if (current_token.type == TOK_KW_ELSE) {
        get_next_token(); 
        
        if (parse_block() != ERR_OK) {
            return ERR_SYNTAX;
        }
    }

    return ERR_OK;
}

// While
int parse_while() {
    get_next_token(); 

    // Podmínka 
    if (parse_expression() != ERR_OK) {
        return ERR_SYNTAX;
    }

    // Do
    if (current_token.type != TOK_KW_DO) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // Tělo while
    if (parse_block() != ERR_OK) {
        return ERR_SYNTAX;
    }

    return ERR_OK;
}