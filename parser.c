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
#include <string.h>
#include <stdlib.h> 

static tToken current_token;

// Načtení dalšího tokenu
static void get_next_token() {
    current_token = scanner_next();
}

// Hlavní funkce
int parse(tSymTable* global_symtable){
    get_next_token();
    if (parse_prolog() != ERR_OK){
        return ERR_SYNTAX;
    }
    while (current_token.type != TOK_EOF)
    {   
        if (current_token.type == TOK_KW_DEF || current_token.type == TOK_ANNOT_MAIN) {
            if (parse_function(global_symtable) != ERR_OK){
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
    return ERR_OK;
}

int parse_val(){
    return ERR_OK;
}

int parse_var(){
    return ERR_OK;
}

int parse_if(){
    return ERR_OK;
}

int parse_while(){
    return ERR_OK;
}

int parse_return(){
    return ERR_OK;
}