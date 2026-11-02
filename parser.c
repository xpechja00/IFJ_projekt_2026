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

int parse_prolog(){
    return ERR_OK;
}

int parse_function(tSymTable* global_symtable){
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