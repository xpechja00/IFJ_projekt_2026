/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   pasrer.c - syntaktický analyzátor
 *
 * Autor: xdundrm00, xpechja00
 *
 */
#include "parser.h"
#include "error.h"
#include "expression.h"
#include "scanner.h"
#include "semantic.h"
#include "symtable.h"
#include <stdlib.h>
#include <string.h>

tToken current_token;

// Pole pro uložení všech tokenů pro dva průchody
static tToken* all_tokens = NULL;
static int token_count = 0;
static int current_token_index = 0;
static tTokenType prev_token_type = TOK_EOF;

// Ukazatel na lokální tabulku symbolů aktuálně parsované funkce
static tSymTable* current_local_table = NULL;

// Kontext sémantické analýzy a typ posledního výrazu
static tSemContext sem_context;
static tTypeInfo last_expr_type = { TYPE_UNIT, TYPE_UNIT };
static const tTypeInfo UNIT_TYPE = { TYPE_UNIT, TYPE_UNIT };

// Počet vstupních funkcí
static int main_function_count = 0;

// Druh posledního dokončeného příkazu (pro pravidlo o if na konci těla funkce)
typedef enum { STMT_OTHER,
    STMT_RETURN,
    STMT_IF } tStmtKind;
static tStmtKind last_stmt_kind = STMT_OTHER;
// Vlastnosti posledního zpracovaného if: všechny větve končí return / všechny větve mají výsledek Unit
static bool last_if_all_return = false;
static bool last_if_all_unit = false;

// Uživatelské funkce z prvního průchodu: pozice v poli tokenů a stav odvození návratového typu
typedef enum { FN_UNRESOLVED,
    FN_IN_PROGRESS,
    FN_RESOLVED } tFuncState;
typedef struct {
    char* name; // ukazuje do tokenu, platí do free_all_tokens
    int def_index; // index tokenu 'def' v all_tokens
    bool explicit_ret; // návratový typ uveden v hlavičce
    tFuncState state; // stav odvození typu (jen pro funkce bez explicitního typu)
} tFuncInfo;
static tFuncInfo* funcs = NULL;
static int func_count = 0;
static int func_capacity = 0;

// Graf volání (hrana caller -> callee) pro kontrolu rekurze
typedef struct {
    int from;
    int to;
} tCallEdge;
static tCallEdge* call_edges = NULL;
static int call_count = 0;
static int call_capacity = 0;

// Index právě zpracovávané funkce ve funcs (-1 mimo funkci)
static int current_func = -1;

// Hloubka vnořené analýzy; > 0 znamená jen kontrolu bez generování kódu
static int check_only_depth = 0;

// Vyhledání uživatelské funkce podle jména; -1, pokud neexistuje
static int find_func(const char* name)
{
    for (int i = 0; i < func_count; i++) {
        if (strcmp(funcs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Přidání funkce z prvního průchodu
static int add_func(char* name, int def_index, bool explicit_ret)
{
    if (func_count >= func_capacity) {
        int new_capacity = (func_capacity == 0) ? 16 : func_capacity * 2;
        tFuncInfo* tmp = realloc(funcs, new_capacity * sizeof(tFuncInfo));
        if (tmp == NULL) {
            return ERR_INTERNAL;
        }
        funcs = tmp;
        func_capacity = new_capacity;
    }
    funcs[func_count].name = name;
    funcs[func_count].def_index = def_index;
    funcs[func_count].explicit_ret = explicit_ret;
    funcs[func_count].state = explicit_ret ? FN_RESOLVED : FN_UNRESOLVED;
    func_count++;
    return ERR_OK;
}

// Přidání hrany do grafu volání
static int add_call_edge(int from, int to)
{
    if (call_count >= call_capacity) {
        int new_capacity = (call_capacity == 0) ? 32 : call_capacity * 2;
        tCallEdge* tmp = realloc(call_edges, new_capacity * sizeof(tCallEdge));
        if (tmp == NULL) {
            return ERR_INTERNAL;
        }
        call_edges = tmp;
        call_capacity = new_capacity;
    }
    call_edges[call_count].from = from;
    call_edges[call_count].to = to;
    call_count++;
    return ERR_OK;
}

// Uvolnění informací o funkcích a grafu volání
static void free_func_info(void)
{
    free(funcs);
    funcs = NULL;
    func_count = func_capacity = 0;
    free(call_edges);
    call_edges = NULL;
    call_count = call_capacity = 0;
    current_func = -1;
    check_only_depth = 0;
}

// Kontrola rekurze: funkce bez explicitního návratového typu nesmí ležet na cyklu
// v grafu volání (spec 2.3, přímá i nepřímá rekurze) -> chyba 6
static int check_recursion(void)
{
    if (func_count == 0) {
        return ERR_OK;
    }
    bool* visited = malloc(func_count * sizeof(bool));
    int* stack = malloc(func_count * sizeof(int));
    if (visited == NULL || stack == NULL) {
        free(visited);
        free(stack);
        return ERR_INTERNAL;
    }

    int result = ERR_OK;
    for (int start = 0; start < func_count && result == ERR_OK; start++) {
        if (funcs[start].explicit_ret) {
            continue;
        }
        // DFS z následníků funkce start; dosažení start znamená cyklus
        for (int i = 0; i < func_count; i++) {
            visited[i] = false;
        }
        int top = 0;
        stack[top++] = start;
        while (top > 0 && result == ERR_OK) {
            int u = stack[--top];
            for (int e = 0; e < call_count; e++) {
                if (call_edges[e].from != u) {
                    continue;
                }
                int v = call_edges[e].to;
                if (v == start) {
                    result = ERR_TYPE;
                    break;
                }
                if (!visited[v]) {
                    visited[v] = true;
                    stack[top++] = v;
                }
            }
        }
    }

    free(visited);
    free(stack);
    return result;
}

int parse_function(tSymTable* global_symtable, bool is_main);
int parse_expression_or_call(void);
int parse_prolog(void);
int parse_statement(void);
int parse_statement_block(void);
int parse_expression(void);
int parse_block(void);
int parse_if(bool as_expr);
int parse_while(void);
int parse_for(void);

// Převod tokenu základního typu na datový typ; false, pokud token není typ
static bool token_to_type(tTokenType tok, tDataType* out)
{
    switch (tok) {
    case TOK_KW_INT:
        *out = TYPE_INT;
        return true;
    case TOK_KW_DOUBLE:
        *out = TYPE_DOUBLE;
        return true;
    case TOK_KW_STRING:
        *out = TYPE_STRING;
        return true;
    case TOK_KW_UNIT:
        *out = TYPE_UNIT;
        return true;
    case TOK_KW_BOOLEAN:
        *out = TYPE_BOOLEAN;
        return true;
    default:
        return false;
    }
}

// Typ začínající tokenem na indexu idx: základní typ nebo List[T] (LISTS).
// Vrátí počet tokenů typu, 0 pokud na indexu typ není (vnořené seznamy nejsou podporovány).
static int type_at(int idx, tTypeInfo* out)
{
    if (idx < 0 || idx >= token_count) {
        return 0;
    }
    out->innerType = TYPE_UNIT;
    if (token_to_type(all_tokens[idx].type, &out->type)) {
        return 1;
    }
    // List [ T ]
    if (all_tokens[idx].type == TOK_KW_LIST && idx + 3 < token_count && all_tokens[idx + 1].type == TOK_LBRACKET && all_tokens[idx + 3].type == TOK_RBRACKET && token_to_type(all_tokens[idx + 2].type, &out->innerType)) {
        out->type = TYPE_LIST;
        return 4;
    }
    return 0;
}

// Typ začínající aktuálním tokenem; po úspěchu je current_token posledním tokenem typu
static bool parse_type(tTypeInfo* out)
{
    int len = type_at(current_token_index - 1, out);
    if (len == 0) {
        return false;
    }
    for (int k = 1; k < len; k++) {
        get_next_token();
    }
    return true;
}

// Uvolnění všech načtených tokenů
static void free_all_tokens(void)
{
    if (all_tokens != NULL) {
        for (int i = 0; i < token_count; i++) {
            token_free(&all_tokens[i]);
        }
        free(all_tokens);
        all_tokens = NULL;
    }
    token_count = 0;
}

// Načtení všech tokenů ze scanneru do dynamického pole
int load_all_tokens(void)
{
    int capacity = 128;
    all_tokens = malloc(capacity * sizeof(tToken));
    if (!all_tokens) {
        return ERR_INTERNAL;
    }

    token_count = 0;
    while (1) {
        tToken token = scanner_next();
        if (token_count >= capacity) {
            capacity *= 2;
            tToken* temp = realloc(all_tokens, capacity * sizeof(tToken));
            if (!temp) {
                token_free(&token);
                free_all_tokens();
                return ERR_INTERNAL;
            }
            all_tokens = temp;
        }
        all_tokens[token_count++] = token;

        if (token.type == TOK_EOF) {
            break;
        }
    }

    return ERR_OK;
}

// Načtení dalšího tokenu
void get_next_token(void)
{
    prev_token_type = current_token.type;
    if (current_token_index < token_count) {
        current_token = all_tokens[current_token_index++];
    } else {
        current_token.type = TOK_EOF;
    }
}

// Naleznutí dalšího tokenu bez spotřebování současného
tToken parser_peek_token(void)
{
    if (current_token_index < token_count) {
        return all_tokens[current_token_index];
    }
    tToken eof = { 0 };
    eof.type = TOK_EOF;
    return eof;
}

// Funkce pro první průchod a registraci hlaviček funkcí (včetně parametrů)
int parse_first_pass(tSymTable* global_symtable)
{
    int i = 0;
    bool is_main = false;
#define TOK(k) (all_tokens[(k)].type)

    while (i < token_count && TOK(i) != TOK_EOF) {
        if (TOK(i) == TOK_ANNOT_MAIN) {
            is_main = true;
            i++;
            continue;
        }

        if (TOK(i) != TOK_KW_DEF) {
            i++;
            continue;
        }

        int def_index = i;
        i++;
        if (i >= token_count || TOK(i) != TOK_IDENT) {
            return ERR_SYNTAX;
        }
        char* func_name = all_tokens[i].value.str_val;
        i++;

        if (i >= token_count || TOK(i) != TOK_LPAREN) {
            return ERR_SYNTAX;
        }
        i++;

        // Parametry: id : Typ, ...
        int capacity = 4;
        int count = 0;
        tTypeInfo* params = malloc(capacity * sizeof(tTypeInfo));
        if (params == NULL) {
            return ERR_INTERNAL;
        }
        while (i < token_count && TOK(i) != TOK_RPAREN) {
            if (i + 2 >= token_count || TOK(i) != TOK_IDENT || TOK(i + 1) != TOK_COLON) {
                free(params);
                return ERR_SYNTAX;
            }
            tTypeInfo p_type;
            int type_len = type_at(i + 2, &p_type);
            if (type_len == 0) {
                free(params);
                return ERR_SYNTAX;
            }
            i += 2 + type_len;

            if (count >= capacity) {
                capacity *= 2;
                tTypeInfo* tmp = realloc(params, capacity * sizeof(tTypeInfo));
                if (tmp == NULL) {
                    free(params);
                    return ERR_INTERNAL;
                }
                params = tmp;
            }
            params[count] = p_type;
            count++;

            if (i < token_count && TOK(i) == TOK_COMMA) {
                i++;
            } else if (i >= token_count || TOK(i) != TOK_RPAREN) {
                free(params);
                return ERR_SYNTAX;
            }
        }
        if (i >= token_count) {
            free(params);
            return ERR_SYNTAX;
        }
        i++; // )

        // Volitelný návratový typ
        tTypeInfo ret_type_info = { TYPE_UNIT, TYPE_UNIT };
        bool explicit_ret = (i < token_count && TOK(i) == TOK_COLON);
        if (explicit_ret) {
            i++;
            int type_len = type_at(i, &ret_type_info);
            if (type_len == 0) {
                free(params);
                return ERR_SYNTAX;
            }
            i += type_len;
        }

        int err = sem_declare_function(global_symtable, func_name, ret_type_info, params, count);
        free(params);
        if (err != ERR_OK) {
            return err;
        }
        err = add_func(func_name, def_index, explicit_ret);
        if (err != ERR_OK) {
            return err;
        }

        if (is_main) {
            main_function_count++;
        }
        is_main = false;
    }
#undef TOK
    return ERR_OK;
}

// Úklid po parsování
static int parse_finish(int err)
{
    sem_cleanup(&sem_context);
    free_func_info();
    free_all_tokens();
    return err;
}

// Hlavní funkce
int parse(tSymTable* global_symtable)
{
    main_function_count = 0;
    free_func_info();

    // Načtení všech tokenů do paměti
    int err = load_all_tokens();
    if (err != ERR_OK) {
        return err;
    }

    // Inicializace sémantického kontextu a vestavěných funkcí
    err = sem_init(&sem_context, global_symtable);
    if (err != ERR_OK) {
        return parse_finish(err);
    }

    current_token_index = 0;
    get_next_token();

    // Prolog
    err = parse_prolog();
    if (err != ERR_OK) {
        return parse_finish(err);
    }

    // První průchod, registrace hlaviček dopředu
    err = parse_first_pass(global_symtable);
    if (err != ERR_OK) {
        return parse_finish(err);
    }

    // Druhý průchod (prolog už je zkontrolovaný, current_token stojí za ním)
    while (current_token.type != TOK_EOF) {
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

        if (current_token.type != TOK_KW_DEF) {
            return parse_finish(ERR_SYNTAX);
        }
        err = parse_function(global_symtable, is_main);
        if (err != ERR_OK) {
            return parse_finish(err);
        }
    }

    // Rekurze přes funkci bez explicitního návratového typu (chyba 6)
    err = check_recursion();
    if (err != ERR_OK) {
        return parse_finish(err);
    }

    // Kontrola existence právě jedné funkce @main
    return parse_finish(sem_check_main_func(main_function_count));
}

// Prolog: import ifj26.*
int parse_prolog(void)
{
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

    // EOL za importem
    if (current_token.type != TOK_EOL) {
        return ERR_SYNTAX;
    }
    get_next_token();

    return ERR_OK;
}

// Funkce def Nazev(id:param, id:param..)
int parse_function(tSymTable* global_symtable, bool is_main)
{
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
    if (current_token.type != TOK_LPAREN) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // Dynamické pole pro parametry
    int param_capacity = 4;
    int param_count = 0;
    tTypeInfo* params_arr = malloc(param_capacity * sizeof(tTypeInfo));
    char** param_names = malloc(param_capacity * sizeof(char*));
    if (params_arr == NULL || param_names == NULL) {
        free(params_arr);
        free(param_names);
        return ERR_INTERNAL;
    }

    // param)
    while (current_token.type != TOK_RPAREN) {
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
        tTypeInfo p_type;
        if (!parse_type(&p_type)) {
            free(params_arr);
            free(param_names);
            return ERR_SYNTAX;
        }
        get_next_token();

        if (param_count >= param_capacity) {
            param_capacity *= 2;

            tTypeInfo* params_arr_pointer = realloc(params_arr, param_capacity * sizeof(tTypeInfo));
            if (!params_arr_pointer) {
                free(params_arr);
                free(param_names);
                return ERR_INTERNAL;
            }
            params_arr = params_arr_pointer;

            char** param_names_pointer = realloc(param_names, param_capacity * sizeof(char*));
            if (!param_names_pointer) {
                free(params_arr);
                free(param_names);
                return ERR_INTERNAL;
            }
            param_names = param_names_pointer;
        }

        params_arr[param_count] = p_type;
        param_names[param_count] = param_name;
        param_count++;

        //,
        if (current_token.type == TOK_COMMA) {
            get_next_token();
            // Koncová čárka je povolena jen tehdy, když ')' je na dalším řádku
            if (current_token.type == TOK_RPAREN && !current_token.nl_before) {
                free(params_arr);
                free(param_names);
                return ERR_SYNTAX;
            }
        }

        //)
        else if (current_token.type == TOK_RPAREN) {
            break;
        }

        else {
            free(params_arr);
            free(param_names);
            return ERR_SYNTAX;
        }
    }
    get_next_token();

    // Volitelný návratový typ
    tTypeInfo ret_type_info = { TYPE_UNIT, TYPE_UNIT };
    bool has_explicit_ret = false;
    if (current_token.type == TOK_COLON) {
        has_explicit_ret = true;
        get_next_token();

        // Datový typ
        if (!parse_type(&ret_type_info)) {
            free(params_arr);
            free(param_names);
            return ERR_SYNTAX;
        }
        get_next_token();
    }
    tDataType ret_type = ret_type_info.type;

    // Funkce, jejíž typ už byl odvozen vnořenou analýzou, si odvozený typ ponechá
    int func_idx = find_func(func_name);
    current_func = func_idx;
    if (!has_explicit_ret && func_idx >= 0 && funcs[func_idx].state == FN_RESOLVED) {
        tData* known = symtable_search(global_symtable, func_name);
        if (known != NULL && known->kind == SYM_FUNC) {
            ret_type_info = known->attribute.func.returnType;
        }
    }

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

    // Během zpracování těla je typ funkce bez explicitního typu rozpracovaný
    if (!has_explicit_ret && func_idx >= 0) {
        funcs[func_idx].state = FN_IN_PROGRESS;
    }

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
        body_err = parse_statement_block();
        if (body_err == ERR_OK && last_stmt_kind == STMT_IF) {
            // Tělo končí příkazem if (spec 2.3, pravidlo 3)
            bool explicit_unit = has_explicit_ret && ret_type == TYPE_UNIT;
            if (explicit_unit || last_if_all_return) {
                body_err = ERR_OK;
            } else if (!has_explicit_ret && last_if_all_unit) {
                body_err = sem_check_func_body_result(&sem_context, UNIT_TYPE);
            } else {
                body_err = ERR_TYPE;
            }
        } else if (body_err == ERR_OK) {
            body_err = sem_check_func_body_result(&sem_context, last_expr_type);
        }
    } else {
        // Jednořádkové tělo
        body_err = parse_expression_or_call();
        if (body_err == ERR_OK) {
            // >> Kontrola návratového typu jednořádkového těla funkce
            body_err = sem_check_func_body_result(&sem_context, last_expr_type);
        }
    }

    // Funkce neměla explicitní návratový typ
    if (body_err == ERR_OK && !has_explicit_ret) {
        tData* func_data = symtable_search(global_symtable, func_name);
        if (func_data != NULL && func_data->kind == SYM_FUNC) {
            func_data->attribute.func.returnType = sem_context.expected_return_type;
        }
        // Vstupní funkce musí mít výsledný typ Unit, i když je odvozený
        if (is_main && sem_context.expected_return_type.type != TYPE_UNIT) {
            body_err = ERR_TYPE;
        } else if (func_idx >= 0) {
            funcs[func_idx].state = FN_RESOLVED;
        }
    }

    // >> Opouštění funkce a úklid lokálního rozsahu platnosti
    sem_leave_function(&sem_context);
    current_local_table = NULL;

    return body_err;
}

int parse_expression()
{
    // >> Precedenční analýza výrazu se sémantickou kontrolou
    return parse_expression_psa(&sem_context, current_local_table, &last_expr_type);
}

// Zjištění zda se jedná o volání funkce či výraz
int parse_expression_or_call()
{
    if (current_token.type == TOK_KW_IF) {
        return parse_if(true);
    }
    return parse_expression();
}

// Var/Val
int parse_var()
{
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
        if (!parse_type(&var_type)) {
            return ERR_SYNTAX;
        }
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
    err = sem_define_var(&sem_context, current_local_table, var_name, is_mutable, has_explicit_type, var_type, last_expr_type);

    // Definice proměnné jako příkaz má typ Unit
    last_expr_type = UNIT_TYPE;
    return err;
}

// Return
int parse_return()
{
    get_next_token();

    if (current_token.type == TOK_EOL || current_token.type == TOK_DEDENT || current_token.type == TOK_SEMICOLON || current_token.type == TOK_KW_ELSE) {
        // >> Sémantická kontrola prázdného příkazu return
        last_expr_type = UNIT_TYPE;
        return sem_check_return(&sem_context, false, UNIT_TYPE);
    }

    int err = parse_expression_or_call();
    if (err != ERR_OK)
        return err;
    // >> Sémantická kontrola návratového typu v příkazu return
    return sem_check_return(&sem_context, true, last_expr_type);
}

// Příkazy
static int parse_statement_inner(void)
{
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
        return parse_if(false);
    }

    // While
    if (current_token.type == TOK_KW_WHILE) {
        return parse_while();
    }

    // For
    if (current_token.type == TOK_KW_FOR) {
        return parse_for();
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
            if (err != ERR_OK)
                return err;
            // >> Kontrola modifikovatelnosti a typové kompatibility při přiřazení
            err = sem_check_assignment(&sem_context, current_local_table, var_name, last_expr_type);
            last_expr_type = UNIT_TYPE; // přiřazení má typ Unit
            return err;
        }
    }

    // Čistý výraz
    return parse_expression_or_call();
}

// Zpracuje jeden příkaz a zapamatuje si jeho druh
int parse_statement(void)
{
    tTokenType first = current_token.type;
    int err = parse_statement_inner();
    if (first == TOK_KW_RETURN) {
        last_stmt_kind = STMT_RETURN;
    } else if (first == TOK_KW_IF) {
        last_stmt_kind = STMT_IF;
    } else {
        last_stmt_kind = STMT_OTHER;
    }
    return err;
}

// Funkce pro zpracování bloku příkazů
int parse_statement_block()
{
    if (current_token.type != TOK_INDENT) {
        return ERR_SYNTAX;
    }
    get_next_token();

    while (current_token.type != TOK_DEDENT && current_token.type != TOK_EOF) {
        int err = parse_statement();
        if (err != ERR_OK) {
            return err;
        }

        if (current_token.type == TOK_SEMICOLON) {
            get_next_token();
        } else if (current_token.type == TOK_EOL) {
            get_next_token();
        } else if (current_token.type != TOK_DEDENT && prev_token_type != TOK_DEDENT && prev_token_type != TOK_EOL) {
            return ERR_SYNTAX;
        }
    }

    if (current_token.type != TOK_DEDENT) {
        return ERR_SYNTAX;
    }
    get_next_token();

    return ERR_OK;
}

// Blok příkazů v if/while
int parse_block()
{
    // >> Vytvoření nového vnořeného rozsahu platnosti (scope)
    int push_err = sem_scope_push(&sem_context);
    if (push_err != ERR_OK) {
        return push_err;
    }
    current_local_table = sem_context.scopes[sem_context.scope_top];

    int err = ERR_OK;
    if (current_token.type == TOK_EOL) {
        get_next_token();
        err = parse_statement_block();
    } else {
        // Jednořádkový příkaz
        err = parse_statement();
        if (err == ERR_OK && current_token.type == TOK_EOL) {
            get_next_token();
        }
    }

    // >> Uzavření aktuálního rozsahu platnosti
    sem_scope_pop(&sem_context);
    current_local_table = (sem_context.scope_top >= 0) ? sem_context.scopes[sem_context.scope_top] : NULL;
    return err;
}

// Zjistí, zda větev končí return a zda má výsledek Unit (podle posledního příkazu větve)
static void branch_info(bool* ends_with_return, bool* is_unit)
{
    if (last_stmt_kind == STMT_RETURN) {
        *ends_with_return = true;
        *is_unit = false;
    } else if (last_stmt_kind == STMT_IF) {
        *ends_with_return = last_if_all_return;
        *is_unit = last_if_all_unit;
    } else {
        *ends_with_return = false;
        *is_unit = (last_expr_type.type == TYPE_UNIT);
    }
}

// Společný typ dvou větví (s rozšířením Int na Double); false, pokud neexistuje
static bool common_type(tTypeInfo a, tTypeInfo b, tTypeInfo* out)
{
    if (sem_types_compatible(a, b)) {
        *out = a;
        return true;
    }
    if (sem_types_compatible(b, a)) {
        *out = b;
        return true;
    }
    return false;
}

// If jako příkaz (as_expr == false) nebo jako výraz (as_expr == true, CONTROL)
// if podmínka then větev [else (větev | if ...)]
int parse_if(bool as_expr)
{
    get_next_token();

    // Podmínka
    int err = parse_expression();
    if (err != ERR_OK) {
        return err;
    }

    // >> Kontrola pravdivostního typu podmínky
    err = sem_check_condition(last_expr_type);
    if (err != ERR_OK) {
        return err;
    }

    // Then
    if (current_token.type != TOK_KW_THEN) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // Tělo then (blok nebo jednořádkové tělo)
    err = parse_block();
    if (err != ERR_OK) {
        return err;
    }
    tTypeInfo then_type = last_expr_type;
    bool then_return, then_unit;
    branch_info(&then_return, &then_unit);

    // Chybějící else se chová jako větev s () (BOOLTHEN)
    tTypeInfo else_type = UNIT_TYPE;
    bool else_return = false;
    bool else_unit = true;

    if (current_token.type == TOK_KW_ELSE) {
        get_next_token();
        if (current_token.type == TOK_KW_IF) {
            // else if
            err = parse_if(as_expr);
            if (err != ERR_OK) {
                return err;
            }
            else_type = last_expr_type;
            else_return = last_if_all_return;
            else_unit = last_if_all_unit;
        } else {
            err = parse_block();
            if (err != ERR_OK) {
                return err;
            }
            else_type = last_expr_type;
            branch_info(&else_return, &else_unit);
        }
    } else if (as_expr) {
        // if jako výraz musí mít větev else
        return ERR_SYNTAX;
    }

    last_if_all_return = then_return && else_return;
    last_if_all_unit = then_unit && else_unit;

    tTypeInfo result_type;
    bool has_common = common_type(then_type, else_type, &result_type);
    if (as_expr) {
        // Typy obou větví musí být shodné (příp. s implicitní konverzí)
        if (!has_common) {
            return ERR_TYPE;
        }
        last_expr_type = result_type;
    } else {
        last_expr_type = has_common ? result_type : UNIT_TYPE;
    }

    return ERR_OK;
}

// While
int parse_while()
{
    get_next_token();

    // Podmínka
    int err = parse_expression();
    if (err != ERR_OK) {
        return err;
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
    err = parse_block();
    if (err != ERR_OK) {
        return err;
    }

    last_expr_type = UNIT_TYPE; // cyklus má typ Unit
    return ERR_OK;
}

// Pomocná funkce: je aktuální token identifikátor s daným jménem? (to/until/by)
static bool current_is_word(const char* word)
{
    return current_token.type == TOK_IDENT && current_token.value.str_val != NULL && strcmp(current_token.value.str_val, word) == 0;
}

// Výraz, který musí být typu Int (meze a krok cyklu for)
static int parse_int_expression(void)
{
    int err = parse_expression();
    if (err != ERR_OK) {
        return err;
    }
    return (last_expr_type.type == TYPE_INT) ? ERR_OK : ERR_TYPE;
}

// For (CONTROL):
//   for id <- expr (to|until) expr [by expr] {if expr} do tělo
//   for EOL INDENT generátor EOL {if expr EOL} DEDENT do tělo
// "to", "until" a "by" jsou identifikátory, ne klíčová slova
int parse_for(void)
{
    get_next_token();

    // Víceřádkový zápis: generátor a stráže jsou v odsazeném bloku
    bool multiline = false;
    if (current_token.type == TOK_EOL) {
        get_next_token();
        if (current_token.type != TOK_INDENT) {
            return ERR_SYNTAX;
        }
        get_next_token();
        multiline = true;
    }

    // Identifikátor řídicí proměnné
    if (current_token.type != TOK_IDENT) {
        return ERR_SYNTAX;
    }
    char* var_name = current_token.value.str_val;
    get_next_token();

    // <-
    if (current_token.type != TOK_ARROW_L) {
        return ERR_SYNTAX;
    }
    get_next_token();

    // Zdroj generátoru: počáteční hodnota rozsahu, nebo seznam (LISTS)
    int err = parse_expression();
    if (err != ERR_OK)
        return err;
    tTypeInfo var_type = { TYPE_INT, TYPE_UNIT };
    bool over_list = (last_expr_type.type == TYPE_LIST);

    if (over_list) {
        // for x <- seznam: proměnná má typ prvku
        if (sem_is_untyped_list(last_expr_type)) {
            return ERR_TYPE;
        }
        var_type.type = last_expr_type.innerType;
    } else {
        if (last_expr_type.type != TYPE_INT) {
            return ERR_TYPE;
        }

        // to / until
        if (!current_is_word("to") && !current_is_word("until")) {
            return ERR_SYNTAX;
        }
        get_next_token();

        // Koncová hodnota
        err = parse_int_expression();
        if (err != ERR_OK)
            return err;
    }

    // Volitelný krok (jen u rozsahu)
    if (!over_list && current_is_word("by")) {
        get_next_token();
        // Krok zadaný samotným literálem 0 je chyba 10
        tToken step_token = current_token;
        int step_start = current_token_index;
        err = parse_int_expression();
        if (err != ERR_OK)
            return err;
        if (current_token_index - step_start == 1 && step_token.type == TOK_INT_LIT && step_token.value.int_val == 0) {
            return ERR_OTHER;
        }
    }

    // Řídicí proměnná platí ve strážích i v těle: vlastní rozsah platnosti
    err = sem_scope_push(&sem_context);
    if (err != ERR_OK)
        return err;
    current_local_table = sem_context.scopes[sem_context.scope_top];

    err = sem_define_var(&sem_context, current_local_table, var_name, false, true, var_type, var_type);

    // Stráže (guards): if podmínka
    while (err == ERR_OK) {
        if (multiline) {
            // Ve víceřádkovém zápisu je každá položka na samostatném řádku
            if (current_token.type != TOK_EOL) {
                err = ERR_SYNTAX;
                break;
            }
            get_next_token();
            if (current_token.type == TOK_DEDENT) {
                get_next_token();
                break;
            }
        }
        if (current_token.type != TOK_KW_IF) {
            if (multiline) {
                err = ERR_SYNTAX;
            }
            break;
        }
        get_next_token();
        err = parse_expression();
        if (err == ERR_OK) {
            err = sem_check_condition(last_expr_type);
        }
    }

    // do
    if (err == ERR_OK && current_token.type != TOK_KW_DO) {
        err = ERR_SYNTAX;
    }

    // Tělo cyklu for
    if (err == ERR_OK) {
        get_next_token();
        err = parse_block();
    }

    sem_scope_pop(&sem_context);
    current_local_table = (sem_context.scope_top >= 0) ? sem_context.scopes[sem_context.scope_top] : NULL;

    last_expr_type = UNIT_TYPE; // cyklus má typ Unit
    return err;
}

// Okamžitá analýza těla funkce bez explicitního návratového typu (jen kontrola, bez generování).
// Uloží celý stav parseru, zpracuje funkci od jejího 'def' a stav obnoví.
static int analyze_function_now(int idx)
{
    tToken saved_token = current_token;
    int saved_index = current_token_index;
    tTokenType saved_prev = prev_token_type;
    tSymTable* saved_local = current_local_table;
    tSemContext saved_ctx = sem_context;
    tTypeInfo saved_expr = last_expr_type;
    tStmtKind saved_kind = last_stmt_kind;
    bool saved_all_return = last_if_all_return;
    bool saved_all_unit = last_if_all_unit;
    int saved_func = current_func;

    // Prázdné rozsahy pro vnořenou analýzu; rozsahy volající funkce se NEuvolňují
    for (int a = 0; a < SEM_MAX_SCOPES; a++) {
        sem_context.scopes[a] = NULL;
    }
    sem_context.scope_top = -1;
    sem_context.in_function = false;

    check_only_depth++;
    current_token_index = funcs[idx].def_index;
    get_next_token();
    int err = parse_function(sem_context.global_table, false);
    check_only_depth--;

    // Úklid rozsahů vnořené analýzy (i po chybě)
    sem_cleanup(&sem_context);

    // Druhý průchod funkci zpracuje znovu, nesmí ji tedy považovat za redefinici
    tData* data = symtable_search(sem_context.global_table, funcs[idx].name);
    if (data != NULL && data->kind == SYM_FUNC) {
        data->attribute.func.isDefined = false;
    }

    current_token = saved_token;
    current_token_index = saved_index;
    prev_token_type = saved_prev;
    current_local_table = saved_local;
    sem_context = saved_ctx;
    last_expr_type = saved_expr;
    last_stmt_kind = saved_kind;
    last_if_all_return = saved_all_return;
    last_if_all_unit = saved_all_unit;
    current_func = saved_func;
    return err;
}

int parser_resolve_function(const char* name)
{
    int idx = find_func(name);
    if (idx < 0) {
        return ERR_OK; // vestavěná nebo nedefinovaná funkce (řeší sem_check_function_call)
    }
    if (current_func >= 0) {
        int err = add_call_edge(current_func, idx);
        if (err != ERR_OK) {
            return err;
        }
    }
    if (funcs[idx].state == FN_RESOLVED) {
        return ERR_OK;
    }
    if (funcs[idx].state == FN_IN_PROGRESS) {
        return ERR_TYPE; // rekurze přes funkci bez návratového typu
    }
    return analyze_function_now(idx);
}

int parser_parse_if_expr(tTypeInfo* out_type)
{
    int err = parse_if(true);
    if (err == ERR_OK && out_type != NULL) {
        *out_type = last_expr_type;
    }
    return err;
}

bool parser_codegen_enabled(void)
{
    return check_only_depth == 0;
}
