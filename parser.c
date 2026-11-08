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

// Ukazatel na lokální tabulku symbolů aktuálně parsované funkce
static tSymTable* current_local_table = NULL;

// Počet vstupních funkcí
static int main_function_count = 0;

int parse_function(tSymTable* global_symtable, bool is_main);
int parse_expression_or_call();
int parse_function_call();
int parse_prolog();
int parse_statement();
int parse_expression();
int parse_block();
int parse_if();
int parse_while();

// Načtení dalšího tokenu (s přípravou na lookahead)
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
            continue;
        }

        bool is_main = false;
        if (current_token.type == TOK_ANNOT_MAIN) {
            is_main = true;
            get_next_token(); 
            if (current_token.type == TOK_EOL) {
                get_next_token();
            }
        }
        
        if (current_token.type == TOK_KW_DEF) {
            if (parse_function(global_symtable, is_main) != ERR_OK) {
                return ERR_SYNTAX;
            }
        } 
        else {
            return ERR_SYNTAX;
        }
    }

    if (main_function_count == 0) {
        return ERR_UNDEF;
    }

    if (main_function_count > 1) {
        return ERR_REDEF;
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
int parse_function(tSymTable* global_symtable, bool is_main){
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

    // Evidence main funkce
    if (is_main) {
        main_function_count++;
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

    // Vytvoření lokální tabulky symbolů pro parametry a lokální proměnné
    tSymTable* local_table = symtable_create();
    if (local_table == NULL) {
        return ERR_INTERNAL;
    }
    current_local_table = local_table;

    // Parametry
    // (
    if (current_token.type != TOK_LPAREN){
        symtable_free(local_table);
        current_local_table = NULL;
        return ERR_SYNTAX;
    }
    get_next_token();
    
    // Dynamické pole pro parametry
    int param_capacity = 4;
    int param_count = 0;
    tTypeInfo* params_arr = malloc(param_capacity * sizeof(tTypeInfo));
    char** param_names = malloc(param_capacity * sizeof(char*));

    // param)
    while (current_token.type != TOK_RPAREN){
        if (current_token.type == TOK_EOL) {
            get_next_token();
            continue;
        }

        // identifikátor parametru
        if (current_token.type != TOK_IDENT) {
            free(params_arr);
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_SYNTAX;
        }
        char* param_name = current_token.value.str_val;
        get_next_token();

        //:
        if (current_token.type != TOK_COLON) { 
            free(params_arr);
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_SYNTAX;
        }
        get_next_token();

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            free(params_arr);
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_SYNTAX;
        }
        
        // Převod typu tokenu
        tDataType p_type = TYPE_UNIT;
        if (current_token.type == TOK_KW_INT){
            p_type = TYPE_INT;
        }

        else if (current_token.type == TOK_KW_DOUBLE){
            p_type = TYPE_DOUBLE;
        }

        else if (current_token.type == TOK_KW_STRING){
            p_type = TYPE_STRING;
        }
        
        else if (current_token.type == TOK_KW_UNIT){
            p_type = TYPE_UNIT;
        }
        get_next_token();
        
        if (param_count >= param_capacity) {
            param_capacity *= 2;
            tTypeInfo* params_arr_pointer = realloc(params_arr, param_capacity * sizeof(tTypeInfo));
            char** param_names_pointer = realloc(param_names, param_capacity * sizeof(char*));
            if (!params_arr_pointer || !param_names_pointer) {
                free(params_arr);
                free(param_names);
                symtable_free(local_table);
                current_local_table = NULL;
                return ERR_INTERNAL;
            }
            params_arr = params_arr_pointer;
            param_names = param_names_pointer;
        }

        params_arr[param_count].type = p_type;
        params_arr[param_count].innerType = TYPE_UNIT;
        param_names[param_count] = param_name;
        param_count++;

        //,
        if (current_token.type == TOK_COMMA) {
            get_next_token(); 
            // Ošetření volitelné čárky před koncem parametrů
            if (current_token.type == TOK_EOL) {
                tToken next = parser_peek_token();
                if (next.type == TOK_RPAREN) {
                    get_next_token(); 
                    break;
                }
            }
        } 

        //)
        else if (current_token.type == TOK_RPAREN) {
            break; 
        }

        else{
            free(params_arr);
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_SYNTAX;
        }
        
    }
    get_next_token();

    // Parametry ve vstupní funkci nejsou povoleny
    if (is_main && param_count > 0) {
        free(params_arr);
        free(param_names);
        symtable_free(local_table);
        current_local_table = NULL;
        return ERR_ARGS;
    }

    // Volitelný návratový typ
    tDataType ret_type = TYPE_UNIT;
    if (current_token.type == TOK_COLON) {
        get_next_token(); 

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            free(params_arr);
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_SYNTAX;
        }
        
        if (current_token.type == TOK_KW_INT){
            ret_type = TYPE_INT;
        }

        else if (current_token.type == TOK_KW_DOUBLE){
            ret_type = TYPE_DOUBLE;
        }
        else if (current_token.type == TOK_KW_STRING){
            ret_type = TYPE_STRING;
        }

        else if (current_token.type == TOK_KW_UNIT){
            ret_type = TYPE_UNIT;
        }
        get_next_token(); 
    }

    // Návratový typ ve vstupní funkci musí být Unit
    if (is_main && ret_type != TYPE_UNIT) {
        free(params_arr);
        free(param_names);
        symtable_free(local_table);
        current_local_table = NULL;
        return ERR_TYPE;
    }

    // Doplnění parametrů a návratového typu zpět do globální tabulky
    tData* function = symtable_search(global_symtable, func_name);
    if (function != NULL) {
        function->attribute.func.paramCount = param_count;
        function->attribute.func.params = params_arr;
        function->attribute.func.returnType.type = ret_type;
    }

    // Vložení dat z parametrů do lokální tabulky
    for (int i = 0; i < param_count; i++) {
        tData param_data;
        param_data.kind = SYM_VAR;
        param_data.attribute.var.isMutable = false;
        param_data.attribute.var.typeInfo = params_arr[i];

        tInsertResult insert = symtable_insert(local_table, param_names[i], param_data);
        if (insert == INSERT_DUPLICATE) {
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_REDEF;
        } else if (insert == INSERT_ERROR) {
            free(param_names);
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_INTERNAL;
        }
    }
    free(param_names);

    //=
    if (current_token.type != TOK_ASSIGN) { 
        symtable_free(local_table);
        current_local_table = NULL;
        return ERR_SYNTAX;
    }
    get_next_token();
    
    // Tělo funkce
    int body_err = 0;
    if (current_token.type == TOK_EOL) {
        get_next_token(); 

        // Začátek bloku
        if (current_token.type != TOK_INDENT) {
            symtable_free(local_table);
            current_local_table = NULL;
            return ERR_SYNTAX;
        }
        get_next_token(); 

        // Příkazy bloku
        while (current_token.type != TOK_DEDENT && current_token.type != TOK_EOF) {
            int err = parse_statement(); 
            if (err != ERR_OK) {
                body_err = err;
                break;
            }

            if (current_token.type == TOK_EOL) {
                get_next_token();
            } else if (current_token.type != TOK_DEDENT) {
                body_err = ERR_SYNTAX;
                break;
            }
        }

        // Konec bloku
        if (body_err == ERR_OK) {
            if (current_token.type != TOK_DEDENT) {
                body_err = ERR_SYNTAX;
            } else {
                get_next_token(); 
            }
        }

    } 
    else {
        // Jednořádkové tělo 
        body_err = parse_expression_or_call();
    }

    // Odklizení lokální tabulky
    symtable_free(local_table);
    current_local_table = NULL;

    return body_err; 
}

int parse_expression() {
    return parse_expression_psa();
}

// Volání funkce
int parse_function_call() {
    get_next_token(); 
    get_next_token(); 
    
    // Parsování argumentů 
    while (current_token.type != TOK_RPAREN) {
        if (current_token.type != TOK_IDENT && current_token.type != TOK_INT_LIT && current_token.type != TOK_DOUBLE_LIT && current_token.type != TOK_STRING_LIT) {
            if (current_token.type == TOK_EOL) { 
                get_next_token(); 
                continue; 
            }
            return ERR_SYNTAX;
        }
        get_next_token(); 

        if (current_token.type == TOK_COMMA) {
            get_next_token();
            if (current_token.type == TOK_EOL) {
                get_next_token();
            }
        }

        else if (current_token.type != TOK_RPAREN) {
            return ERR_SYNTAX;
        }
    }
    get_next_token(); 
    return ERR_OK;
}

// Zjištění zda se jedná o volání funkce či výraz
int parse_expression_or_call() {
    if (current_token.type == TOK_IDENT) {
        tToken next = parser_peek_token();
        if (next.type == TOK_LPAREN) {
            return parse_function_call();
        }
    }
    return parse_expression();
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
    bool is_mutable = (current_token.type == TOK_KW_VAR);
    get_next_token();

    // Identifikátor
    if (current_token.type != TOK_IDENT) {
        return ERR_SYNTAX;
    }
    char* var_name = current_token.value.str_val;
    get_next_token();

    tTypeInfo var_type;
    var_type.type = TYPE_UNIT;
    var_type.innerType = TYPE_UNIT;

    // :
    if (current_token.type == TOK_COLON) {
        get_next_token();

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            return ERR_SYNTAX;
        }

        if (current_token.type == TOK_KW_INT) var_type.type = TYPE_INT;
        else if (current_token.type == TOK_KW_DOUBLE) var_type.type = TYPE_DOUBLE;
        else if (current_token.type == TOK_KW_STRING) var_type.type = TYPE_STRING;
        else if (current_token.type == TOK_KW_UNIT) var_type.type = TYPE_UNIT;
        get_next_token();
    }

    // =
    if (current_token.type != TOK_ASSIGN) {
        return ERR_SYNTAX;
    }
    get_next_token();
    
    int err = parse_expression_or_call();
    if (err != ERR_OK) {
        return err;
    }

    // Vložení lokální proměnné do lokální tabulky symbolů
    if (current_local_table != NULL) {
        tData v_data;
        v_data.kind = SYM_VAR;
        v_data.attribute.var.isMutable = is_mutable;
        v_data.attribute.var.typeInfo = var_type;

        tInsertResult res = symtable_insert(current_local_table, var_name, v_data);
        if (res == INSERT_DUPLICATE) {
            return ERR_REDEF;
        } else if (res == INSERT_ERROR) {
            return ERR_INTERNAL;
        }
    }

    return ERR_OK;
}

// Return
int parse_return() {
    get_next_token(); 

    if (current_token.type == TOK_EOL || current_token.type == TOK_DEDENT) {
        return ERR_OK; 
    }

    return parse_expression_or_call();
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

    // Přiřazení do proměnné nebo volání funkce
    if (current_token.type == TOK_IDENT) {
        char* var_name = current_token.value.str_val; 
        // Zjištění dalšího tokenu
        tToken next = parser_peek_token(); 
        
        if (next.type == TOK_ASSIGN) {
            // Kontrola v tabulce symbolů, zda jde o var
            if (current_local_table != NULL) {
                tData* v_data = symtable_search(current_local_table, var_name);
                if (v_data != NULL && v_data->kind == SYM_VAR && !v_data->attribute.var.isMutable) {
                    return ERR_ASSIGN; 
                }
            }

            // Posuny za identifikátor a =
            get_next_token(); 
            get_next_token(); 
            return parse_expression_or_call(); 
        }
        else if (next.type == TOK_LPAREN) {
            return parse_function_call();
        }
    }

    // Čistý výraz
    return parse_expression_or_call();
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

    // Else
    if (current_token.type != TOK_KW_ELSE) {
        return ERR_SYNTAX;
    }
    get_next_token(); 
    
    // Tělo else
    if (parse_block() != ERR_OK) {
        return ERR_SYNTAX;
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