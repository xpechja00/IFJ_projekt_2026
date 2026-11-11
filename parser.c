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
#include "semantic.h"
#include <string.h>
#include <stdlib.h> 

static tToken current_token;
static tToken lookahead_token;  
static bool has_lookahead = false; 

// Ukazatel na lokální tabulku symbolů aktuálně parsované funkce
static tSymTable* current_local_table = NULL;

// Kontext sémantické analýzy a typ posledního výrazu
static tSemContext sem_context;
static tTypeInfo last_expr_type = { TYPE_UNIT, TYPE_UNIT };

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
    // >> Inicializace sémantického kontextu a vestavěných funkcí
    sem_init(&sem_context, global_symtable);

    get_next_token();
    if (parse_prolog() != ERR_OK){
        // >> Úklid sémantického kontextu
        sem_cleanup(&sem_context);
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
                // >> Úklid sémantického kontextu
                sem_cleanup(&sem_context);
                return ERR_SYNTAX;
            }
        } 
        else {
            // >> Úklid sémantického kontextu
            sem_cleanup(&sem_context);
            return ERR_SYNTAX;
        }
    }

    // >> Kontrola existence právě jedné funkce @main
    int err = sem_check_main_func(main_function_count);
    // >> Úklid sémantického kontextu
    sem_cleanup(&sem_context);
    return err;
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

    // Parametry
    // (
    if (current_token.type != TOK_LPAREN){
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
            return ERR_SYNTAX;
        }
        char* param_name = current_token.value.str_val;
        get_next_token();

        //:
        if (current_token.type != TOK_COLON) { 
            free(params_arr);
            free(param_names);
            return ERR_SYNTAX;
        }
        get_next_token();

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            free(params_arr);
            free(param_names);
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
            return ERR_SYNTAX;
        }
        
    }
    get_next_token();

    // Volitelný návratový typ
    tDataType ret_type = TYPE_UNIT;
    bool has_explicit_ret = false;
    if (current_token.type == TOK_COLON) {
        has_explicit_ret = true;
        get_next_token(); 

        // Datový typ
        if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT) {
            free(params_arr);
            free(param_names);
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

    tTypeInfo ret_type_info = { ret_type, TYPE_UNIT };
    // >> Registrace hlavičky funkce do globální tabulky symbolů
    int sem_err = sem_register_function(global_symtable, func_name, ret_type_info, params_arr, param_count, is_main);
    if (sem_err != ERR_OK) {
        free(params_arr);
        free(param_names);
        return sem_err;
    }

    // >> Vstup do těla funkce a vložení parametrů do lokální tabulky
    sem_err = sem_enter_function(&sem_context, ret_type_info, has_explicit_ret, param_names, params_arr, param_count);
    free(params_arr);
    free(param_names);
    if (sem_err != ERR_OK) {
        return sem_err;
    }
    current_local_table = sem_context.scopes[sem_context.scope_top];

    //=
    if (current_token.type != TOK_ASSIGN) { 
        // >> Opouštění funkce a úklid lokálního rozsahu platnosti
        sem_leave_function(&sem_context);
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
            // >> Opouštění funkce a úklid lokálního rozsahu platnosti
            sem_leave_function(&sem_context);
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
        if (body_err == ERR_OK) {
            // >> Kontrola návratového typu jednořádkového těla funkce
            body_err = sem_check_func_body_result(&sem_context, last_expr_type);
        }
    }

    // >> Opouštění funkce a úklid lokálního rozsahu platnosti
    sem_leave_function(&sem_context);
    current_local_table = NULL;

    return body_err; 
}

int parse_expression() {
    // >> Precedenční analýza výrazu se sémantickou kontrolou
    return parse_expression_psa(&sem_context, current_local_table, &last_expr_type);
}

// Volání funkce
int parse_function_call() {
    char* func_name = current_token.value.str_val;
    get_next_token(); 
    get_next_token(); 
    
    tTypeInfo arg_types[64];
    int arg_count = 0;

    // Parsování argumentů 
    while (current_token.type != TOK_RPAREN) {
        if (current_token.type != TOK_IDENT && current_token.type != TOK_INT_LIT && current_token.type != TOK_DOUBLE_LIT && current_token.type != TOK_STRING_LIT) {
            if (current_token.type == TOK_EOL) { 
                get_next_token(); 
                continue; 
            }
            return ERR_SYNTAX;
        }

        tTypeInfo arg_type;
        // >> Ověření existence a zjištění typu argumentu
        int term_err = sem_resolve_term(&sem_context, current_local_table, current_token, &arg_type);
        if (term_err != ERR_OK) {
            return term_err;
        }
        arg_types[arg_count++] = arg_type;

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

    // >> Sémantická kontrola volání funkce a jejích argumentů
    return sem_check_function_call(sem_context.global_table, func_name, arg_types, arg_count, &last_expr_type);
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
    bool has_explicit_type = false;

    // :
    if (current_token.type == TOK_COLON) {
        has_explicit_type = true;
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

    // >> Kontrola typu a uložení proměnné do tabulky symbolů
    return sem_define_var(&sem_context, current_local_table, var_name, is_mutable, has_explicit_type, var_type, last_expr_type);
}

// Return
int parse_return() {
    get_next_token(); 

    if (current_token.type == TOK_EOL || current_token.type == TOK_DEDENT) {
        tTypeInfo unit_type = { TYPE_UNIT, TYPE_UNIT };
        // >> Sémantická kontrola prázdného příkazu return
        return sem_check_return(&sem_context, false, unit_type);
    }

    int err = parse_expression_or_call();
    if (err != ERR_OK) return err;
    // >> Sémantická kontrola návratového typu v příkazu return
    return sem_check_return(&sem_context, true, last_expr_type);
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
            // Posuny za identifikátor a =
            get_next_token(); 
            get_next_token(); 
            int err = parse_expression_or_call();
            if (err != ERR_OK) return err;
            // >> Kontrola modifikovatelnosti a typové kompatibility při přiřazení
            return sem_check_assignment(&sem_context, current_local_table, var_name, last_expr_type);
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
    // >> Vytvoření nového vnořeného rozsahu platnosti (scope)
    sem_scope_push(&sem_context);
    current_local_table = sem_context.scopes[sem_context.scope_top];

    if (current_token.type == TOK_EOL) {
        get_next_token(); 

        if (current_token.type != TOK_INDENT) {
            // >> Uzavření aktuálního rozsahu platnosti
            sem_scope_pop(&sem_context);
            return ERR_SYNTAX;
        }
        get_next_token(); 

        while (current_token.type != TOK_DEDENT && current_token.type != TOK_EOF) {
            int err = parse_statement(); 
            if (err != ERR_OK) {
                // >> Uzavření aktuálního rozsahu platnosti
                sem_scope_pop(&sem_context);
                return err;
            }

            if (current_token.type == TOK_EOL) {
                get_next_token();
            } else if (current_token.type != TOK_DEDENT) {
                // >> Uzavření aktuálního rozsahu platnosti
                sem_scope_pop(&sem_context);
                return ERR_SYNTAX; 
            }
        }

        if (current_token.type != TOK_DEDENT) {
            // >> Uzavření aktuálního rozsahu platnosti
            sem_scope_pop(&sem_context);
            return ERR_SYNTAX;
        }
        get_next_token(); 
    } else {
        // Jednořádkový příkaz
        if (parse_statement() != ERR_OK) {
            // >> Uzavření aktuálního rozsahu platnosti
            sem_scope_pop(&sem_context);
            return ERR_SYNTAX;
        }
    }

    // >> Uzavření aktuálního rozsahu platnosti
    sem_scope_pop(&sem_context);
    current_local_table = (sem_context.scope_top >= 0) ? sem_context.scopes[sem_context.scope_top] : NULL;
    return ERR_OK;
}

// If
int parse_if() {
    get_next_token(); 

    // Podmínka
    if (parse_expression() != ERR_OK) {
        return ERR_SYNTAX;
    }
    // >> Kontrola pravdivostního typu podmínky
    if (sem_check_condition(last_expr_type) != ERR_OK) {
        return ERR_TYPE;
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
    // >> Kontrola pravdivostního typu podmínky
    if (sem_check_condition(last_expr_type) != ERR_OK) {
        return ERR_TYPE;
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