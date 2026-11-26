/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   expression.c - precedenční syntaktická analýza
 * Autor: xdundrm00
 */
#include "expression.h"
#include "parser.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Indexy pro precedenční tabulku
typedef enum {
    E_PLUS_MINUS, // + -
    E_UNARY_MINUS, // - (unární)
    E_LOG_NOT, // ! (unární negace)
    E_MUL_DIV, // * /
    E_LOG_AND, // &&
    E_LOG_OR, // ||
    E_REL, // relační operátory (<, <=, >, >=)
    E_EQ, // rovnost (==, !=), nižší priorita než E_REL
    E_CONCAT, // ++
    E_OPERAND, // operandy (identifikátory i, literály, true/false)
    E_LPAREN, // (
    E_RPAREN, // )
    E_DOLLAR, // $ (dno zásobníku / konec vstupu)
    E_EXPR, // Neterminál E
    E_NONE
} tPrecedenceSymbol;

// Položka na zásobníku
typedef struct tStackItem {
    tPrecedenceSymbol symbol;
    tToken token;
    tTypeInfo type;
    tExpr* expr;
    struct tStackItem* next;
} tStackItem;

// Zásobník
typedef struct {
    tStackItem* top;
} tStack;

// Precedenční tabulka (vygenerovaná z priorit operátorů dle specifikace, sekce 3 a BOOLTHEN)
static const char precedence_table[13][13] = {
    //               + -    u-    !    * /    &&    ||   rel  == !=   ++    i     (     )     $
    /* + -      */ { '>', '<', '<', '<', '>', '>', '>', '>', '>', '<', '<', '>', '>' },
    /* unární - */ { '>', '<', '<', '>', '>', '>', '>', '>', '>', '<', '<', '>', '>' },
    /* !        */ { '>', '<', '<', '>', '>', '>', '>', '>', '>', '<', '<', '>', '>' },
    /* * /      */ { '>', '<', '<', '>', '>', '>', '>', '>', '>', '<', '<', '>', '>' },
    /* &&       */ { '<', '<', '<', '<', '>', '>', '<', '<', '<', '<', '<', '>', '>' },
    /* ||       */ { '<', '<', '<', '<', '<', '>', '<', '<', '<', '<', '<', '>', '>' },
    /* rel      */ { '<', '<', '<', '<', '>', '>', ' ', '>', '<', '<', '<', '>', '>' },
    /* == !=    */ { '<', '<', '<', '<', '>', '>', '<', ' ', '<', '<', '<', '>', '>' },
    /* ++       */ { '>', '<', '<', '<', '>', '>', '>', '>', '>', '<', '<', '>', '>' },
    /* i        */ { '>', ' ', ' ', '>', '>', '>', '>', '>', '>', ' ', ' ', '>', '>' },
    /* (        */ { '<', '<', '<', '<', '<', '<', '<', '<', '<', '<', '<', '=', ' ' },
    /* )        */ { '>', ' ', ' ', '>', '>', '>', '>', '>', '>', ' ', ' ', '>', '>' },
    /* $        */ { '<', '<', '<', '<', '<', '<', '<', '<', '<', '<', '<', ' ', 'A' },
};

// Pomocné funkce pro zásobník
static void stack_init(tStack* s)
{
    s->top = NULL;
}

static int stack_push(tStack* s, tPrecedenceSymbol symbol, tToken token, tTypeInfo type)
{
    tStackItem* newItem = malloc(sizeof(tStackItem));
    if (newItem == NULL) {
        return 0;
    }
    newItem->symbol = symbol;
    newItem->token = token;
    newItem->type = type;
    newItem->expr = NULL;
    newItem->next = s->top;
    s->top = newItem;
    return 1;
}
// Vloží na zásobník neterminál E i se stromem výrazu (strom při chybě uvolní)
static int stack_push_expr(tStack* s, tTypeInfo type, tExpr* expr)
{
    tToken dummy = { 0 };
    if (!stack_push(s, E_EXPR, dummy, type)) {
        cg_expr_free(expr);
        return 0;
    }
    s->top->expr = expr;
    return 1;
}
// Vyjme strom výrazu z položky (položka ho už nevlastní)
static tExpr* take_expr(tStackItem* item)
{
    tExpr* e = item->expr;
    item->expr = NULL;
    return e;
}

static void stack_pop(tStack* s)
{
    if (s->top != NULL) {
        tStackItem* temp = s->top;
        s->top = s->top->next;
        free(temp);
    }
}

static tPrecedenceSymbol stack_top_terminal(tStack* s)
{
    tStackItem* curr = s->top;
    while (curr != NULL) {
        if (curr->symbol != E_EXPR) {
            return curr->symbol;
        }
        curr = curr->next;
    }
    return E_DOLLAR;
}

static void stack_free(tStack* s)
{
    while (s->top != NULL) {
        cg_expr_free(s->top->expr);
        stack_pop(s);
    }
}

// Uvolní pole stromů výrazů (argumenty volání, prvky seznamu)
static void free_expr_array(tExpr** exprs, int count)
{
    if (exprs == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        cg_expr_free(exprs[i]);
    }
    free(exprs);
}

// Mapování tokenu
static tPrecedenceSymbol token_to_precedence_symbol(tToken* token)
{
    switch (token->type) {
    case TOK_NOT:
        return E_LOG_NOT;
    case TOK_AND:
        return E_LOG_AND;
    case TOK_OR:
        return E_LOG_OR;
    case TOK_CONCAT:
        return E_CONCAT;
    case TOK_PLUS:
    case TOK_MINUS:
        return E_PLUS_MINUS;
    case TOK_STAR:
    case TOK_SLASH:
        return E_MUL_DIV;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        return E_REL;
    case TOK_EQ:
    case TOK_NEQ:
        return E_EQ;
    case TOK_IDENT:
    case TOK_INT_LIT:
    case TOK_DOUBLE_LIT:
    case TOK_STRING_LIT:
    case TOK_TRUE_LIT:
    case TOK_FALSE_LIT:
        return E_OPERAND;
    case TOK_LPAREN:
        return E_LPAREN;
    case TOK_RPAREN:
        return E_RPAREN;
    default:
        return E_NONE;
    }
}

// Precedenční analýza výrazu

// Je aktuální token identifikátor s daným jménem? (to, until, by, range, toList)
static bool current_is_word(const char* word)
{
    return current_token.type == TOK_IDENT && current_token.value.str_val != NULL && strcmp(current_token.value.str_val, word) == 0;
}

// Je jméno viditelná proměnná? (proměnná zastiňuje funkci, např. pro indexování seznamu lst(0))
static bool is_variable(tSemContext* ctx, tSymTable* local, const char* name)
{
    if (name == NULL) {
        return false;
    }
    if (ctx != NULL && ctx->scope_top >= 0) {
        return sem_find_var(ctx, name) != NULL;
    }
    if (local != NULL) {
        tData* d = symtable_search(local, name);
        return d != NULL && d->kind == SYM_VAR;
    }
    return false;
}

// Seznam argumentů "( výraz, výraz, ... )", current_token je '('.
// Typy argumentů vrátí v dynamicky alokovaném poli *out_types (uvolňuje volající).
static int parse_arg_list(tSemContext* ctx, tSymTable* local, tTypeInfo** out_types, tExpr*** out_exprs, int* out_count)
{
    *out_types = NULL;
    *out_exprs = NULL;
    *out_count = 0;

    // (
    if (current_token.type != TOK_LPAREN) {
        return ERR_SYNTAX;
    }
    get_next_token();

    int capacity = 8;
    int count = 0;
    tTypeInfo* types = malloc(capacity * sizeof(tTypeInfo));
    tExpr** exprs = malloc(capacity * sizeof(tExpr*));
    if (types == NULL || exprs == NULL) {
        free(types);
        free(exprs);
        return ERR_INTERNAL;
    }

    // Parsování argumentů
    while (current_token.type != TOK_RPAREN) {
        if (current_token.type == TOK_EOL) {
            get_next_token();
            continue;
        }
        if (current_token.type == TOK_EOF) {
            free(types);
            free_expr_array(exprs, count);
            return ERR_SYNTAX;
        }

        tTypeInfo arg_type;
        tExpr* arg_expr = NULL;
        int expr_err = parse_expression_psa(ctx, local, &arg_type, &arg_expr);
        if (expr_err != ERR_OK) {
            free(types);
            free_expr_array(exprs, count);
            return expr_err;
        }

        if (count >= capacity) {
            capacity *= 2;
            tTypeInfo* tmp = realloc(types, capacity * sizeof(tTypeInfo));
            tExpr** tmp_e = (tmp != NULL) ? realloc(exprs, capacity * sizeof(tExpr*)) : NULL;
            if (tmp != NULL) {
                types = tmp;
            }
            if (tmp_e == NULL) {
                cg_expr_free(arg_expr);
                free(types);
                free_expr_array(exprs, count);
                return ERR_INTERNAL;
            }
            exprs = tmp_e;
        }
        types[count] = arg_type;
        exprs[count] = arg_expr;
        count++;

        if (current_token.type == TOK_COMMA) {
            get_next_token();
            // Koncová čárka je povolena jen tehdy, když ')' je na dalším řádku
            if (current_token.type == TOK_RPAREN && !current_token.nl_before) {
                free(types);
                free_expr_array(exprs, count);
                return ERR_SYNTAX;
            }
        } else if (current_token.type != TOK_RPAREN) {
            free(types);
            free_expr_array(exprs, count);
            return ERR_SYNTAX;
        }
    }
    get_next_token(); // )

    *out_types = types;
    *out_exprs = exprs;
    *out_count = count;
    return ERR_OK;
}

// Pomocná funkce pro parsování volání funkce uvnitř výrazu (FUNEXP)
static int parse_expr_function_call(tSemContext* ctx, tSymTable* local, tToken ident_token, tTypeInfo* out_type, tExpr** out_expr)
{
    char* func_name = ident_token.value.str_val;

    tTypeInfo* arg_types;
    tExpr** arg_exprs;
    int arg_count;
    int err = parse_arg_list(ctx, local, &arg_types, &arg_exprs, &arg_count);
    if (err != ERR_OK) {
        return err;
    }

    // Funkce bez explicitního návratového typu definovaná později: odvodit typ teď
    err = parser_resolve_function(func_name);

    // >> Sémantická kontrola volání funkce a jejích argumentů
    if (err == ERR_OK) {
        err = sem_check_function_call(ctx->global_table, func_name, arg_types, arg_count, out_type);
    }
    free(arg_types);
    if (err != ERR_OK) {
        free_expr_array(arg_exprs, arg_count);
        return err;
    }
    // Argument typu Int pro parametr typu Double: převod (x + 0.0 vygeneruje INT2FLOAT)
    tData* f_data = symtable_search(ctx->global_table, func_name);
    if (f_data != NULL && f_data->kind == SYM_FUNC && strcmp(func_name, "print") != 0 && strcmp(func_name, "toStr") != 0) {
        for (int i = 0; i < arg_count && i < f_data->attribute.func.paramCount; i++) {
            if (f_data->attribute.func.params[i].type == TYPE_DOUBLE && arg_exprs[i]->type.type == TYPE_INT) {
                tTypeInfo dbl = { TYPE_DOUBLE, TYPE_UNIT };
                arg_exprs[i] = cg_expr_binary(TOK_PLUS, arg_exprs[i], cg_expr_double(0.0), dbl);
            }
        }
    }
    // Strom volání přebírá pole argumentů
    *out_expr = cg_expr_call(func_name, arg_exprs, arg_count, *out_type);
    return ERR_OK;
}

// LISTS: List(v1, v2, ...) nebo List.range(start, end[, step]), current_token je List
static int parse_list_value(tSemContext* ctx, tSymTable* local, tTypeInfo* out_type, tExpr** out_expr)
{
    get_next_token(); // List

    tTypeInfo* types;
    tExpr** exprs;
    int count;
    int err;

    // Statická funkce List.range
    if (current_token.type == TOK_DOT) {
        get_next_token();
        if (!current_is_word("range")) {
            return current_token.type == TOK_IDENT ? ERR_UNDEF : ERR_SYNTAX;
        }
        get_next_token();

        err = parse_arg_list(ctx, local, &types, &exprs, &count);
        if (err != ERR_OK) {
            return err;
        }
        if (count != 2 && count != 3) {
            err = ERR_ARGS;
        }
        for (int i = 0; i < count && err == ERR_OK; i++) {
            if (types[i].type != TYPE_INT) {
                err = ERR_ARGS;
            }
        }
        free(types);
        out_type->type = TYPE_LIST;
        out_type->innerType = TYPE_INT;
        if (err != ERR_OK) {
            free_expr_array(exprs, count);
            return err;
        }
        // List.range(start, end[, step]) = rozsah bez horní meze
        *out_expr = cg_expr_range(exprs[0], exprs[1], (count == 3) ? exprs[2] : NULL, false);
        free(exprs); // prvky převzal strom rozsahu
        return ERR_OK;
    }

    // Výčet prvků
    err = parse_arg_list(ctx, local, &types, &exprs, &count);
    if (err != ERR_OK) {
        return err;
    }

    // Prvky musí mít kompatibilní typ (Int a Double dají Double); vnořené seznamy nejsou podporovány
    tTypeInfo elem = { TYPE_LIST, TYPE_UNIT };
    for (int i = 0; i < count && err == ERR_OK; i++) {
        if (types[i].type == TYPE_LIST) {
            err = ERR_TYPE;
        } else if (i == 0 || sem_types_compatible(elem, types[i])) {
            if (i == 0) {
                elem = types[i];
            }
        } else if (sem_types_compatible(types[i], elem)) {
            elem = types[i];
        } else {
            err = ERR_TYPE;
        }
    }
    free(types);

    // Prázdný seznam List() nemá známý typ prvku (innerType == TYPE_LIST)
    out_type->type = TYPE_LIST;
    out_type->innerType = (count == 0) ? TYPE_LIST : elem.type;
    if (err != ERR_OK) {
        free_expr_array(exprs, count);
        return err;
    }
    *out_expr = cg_expr_list_lit(exprs, count, *out_type);
    return ERR_OK;
}

// Zpracuje posloupnost přípon za hotovým výrazem a upraví jeho typ:
//   .id         člen hodnoty (VALMEM) nebo bezparametrický člen seznamu (head, tail, toString)
//   .id(args)   člen seznamu s argumenty (drop, take, slice)
//   (index)     indexování seznamu (LISTS)
static int parse_members(tSemContext* ctx, tSymTable* local, tTypeInfo* type, tExpr** expr)
{
    while (1) {
        // Indexování seznamu: výraz(index)
        if (current_token.type == TOK_LPAREN) {
            if (type->type != TYPE_LIST || sem_is_untyped_list(*type)) {
                return ERR_TYPE;
            }
            get_next_token(); // (
            tTypeInfo index_type;
            tExpr* index_expr = NULL;
            int err = parse_expression_psa(ctx, local, &index_type, &index_expr);
            if (err != ERR_OK) {
                return err;
            }
            if (index_type.type != TYPE_INT) {
                cg_expr_free(index_expr);
                return ERR_TYPE;
            }
            if (current_token.type != TOK_RPAREN) {
                cg_expr_free(index_expr);
                return ERR_SYNTAX;
            }
            get_next_token(); // )
            type->type = type->innerType;
            type->innerType = TYPE_UNIT;
            *expr = cg_expr_list_index(*expr, index_expr, *type);
            continue;
        }

        if (current_token.type != TOK_DOT) {
            return ERR_OK;
        }
        get_next_token(); // '.'
        if (current_token.type != TOK_IDENT || current_token.value.str_val == NULL) {
            return ERR_SYNTAX;
        }
        const char* member = current_token.value.str_val;

        // Členy hodnot (VALMEM)
        if (type->type != TYPE_LIST) {
            int err = sem_check_member(*type, member, type);
            if (err != ERR_OK) {
                return err;
            }
            get_next_token(); // id
            *expr = cg_expr_member(*expr, member, NULL, 0, *type);
            continue;
        }

        // Členy seznamu (LISTS)
        int arg_count;
        tTypeInfo result_type;
        int err = sem_check_list_member(*type, member, &arg_count, &result_type);
        if (err != ERR_OK) {
            return err;
        }
        get_next_token(); // id

        tExpr** args = NULL;
        int count = 0;
        if (arg_count > 0) {
            // drop(n), take(n), slice(from, until): argumenty typu Int
            tTypeInfo* types;
            err = parse_arg_list(ctx, local, &types, &args, &count);
            if (err != ERR_OK) {
                return err;
            }
            if (count != arg_count) {
                err = ERR_ARGS;
            }
            for (int i = 0; i < count && err == ERR_OK; i++) {
                if (types[i].type != TYPE_INT) {
                    err = ERR_ARGS;
                }
            }
            free(types);
            if (err != ERR_OK) {
                free_expr_array(args, count);
                return err;
            }
        }
        *type = result_type;
        *expr = cg_expr_member(*expr, member, args, count, *type);
    }
}

int parse_expression_psa(tSemContext* ctx, tSymTable* local, tTypeInfo* out_type, tExpr** out_expr)
{
    tStack stack;
    stack_init(&stack);

    tToken dummy_token = { 0 };
    tTypeInfo dummy_type = { TYPE_UNIT, TYPE_UNIT };

    // Vložení '$' na dno zásobníku
    if (!stack_push(&stack, E_DOLLAR, dummy_token, dummy_type)) {
        stack_free(&stack);
        return ERR_INTERNAL;
    }

    int result = ERR_OK;

    while (1) {
        tPrecedenceSymbol top_term = stack_top_terminal(&stack);
        tPrecedenceSymbol input_sym = token_to_precedence_symbol(&current_token);

        // Stojí na vrcholu zásobníku operand? (rozhoduje o unárnosti a konci výrazu)
        tPrecedenceSymbol top_sym = stack.top->symbol;
        bool after_operand = (top_sym == E_EXPR || top_sym == E_OPERAND || top_sym == E_RPAREN);

        // + a - bez levého operandu jsou unární
        if (input_sym == E_PLUS_MINUS && !after_operand) {
            input_sym = E_UNARY_MINUS;
        }
        // LISTS: rozsah (a to b [by c]).toList uvnitř závorek
        if (top_sym == E_EXPR && stack.top->next != NULL && stack.top->next->symbol == E_LPAREN && (current_is_word("to") || current_is_word("until"))) {
            if (stack.top->type.type != TYPE_INT) {
                result = ERR_TYPE;
                break;
            }
            bool inclusive = current_is_word("to");
            get_next_token(); // to / until
            tTypeInfo bound_type;
            tExpr* end_expr = NULL;
            tExpr* step_expr = NULL;
            int re = parse_expression_psa(ctx, local, &bound_type, &end_expr);
            if (re == ERR_OK && bound_type.type != TYPE_INT)
                re = ERR_TYPE;
            if (re == ERR_OK && current_is_word("by")) {
                get_next_token();
                re = parse_expression_psa(ctx, local, &bound_type, &step_expr);
                if (re == ERR_OK && bound_type.type != TYPE_INT)
                    re = ERR_TYPE;
            }
            if (re == ERR_OK && current_token.type != TOK_RPAREN)
                re = ERR_SYNTAX;
            if (re != ERR_OK) {
                cg_expr_free(end_expr);
                cg_expr_free(step_expr);
                result = re;
                break;
            }
            get_next_token(); // )
            tExpr* start_expr = take_expr(stack.top);
            stack_pop(&stack); // E
            stack_pop(&stack); // (
            tExpr* range_expr = cg_expr_range(start_expr, end_expr, step_expr, inclusive);
            // Rozsah nelze použít jako hodnotu, jen převést členem toList
            if (current_token.type != TOK_DOT) {
                cg_expr_free(range_expr);
                result = ERR_SYNTAX;
                break;
            }
            get_next_token(); // .
            if (!current_is_word("toList")) {
                cg_expr_free(range_expr);
                result = (current_token.type == TOK_IDENT) ? ERR_UNDEF : ERR_SYNTAX;
                break;
            }
            get_next_token(); // toList
            tTypeInfo range_list = { TYPE_LIST, TYPE_INT };
            re = parse_members(ctx, local, &range_list, &range_expr);
            if (re != ERR_OK) {
                cg_expr_free(range_expr);
                result = re;
                break;
            }
            if (!stack_push_expr(&stack, range_list, range_expr)) {
                result = ERR_INTERNAL;
                break;
            }
            continue;
        }

        // '(' hned za operandem je indexování seznamu: nejdřív se zredukuje operand
        // a závorku pak zpracuje parse_members
        if (input_sym == E_LPAREN && after_operand) {
            input_sym = E_NONE;
        }
        // Operand hned za operandem ukončuje výraz (např. "1 to 10" v cyklu for)
        if (input_sym == E_OPERAND && after_operand) {
            input_sym = E_NONE;
        }
        // ')' bez odpovídající '(' ukončuje výraz (konec argumentu volání)
        if (input_sym == E_RPAREN && top_term == E_DOLLAR) {
            input_sym = E_NONE;
        }
        tPrecedenceSymbol table_input_sym = (input_sym == E_NONE) ? E_DOLLAR : input_sym;

        char relation = precedence_table[top_term][table_input_sym];

        // Literál () typu Unit: '(' hned následovaná ')' na místě operandu
        if (input_sym == E_LPAREN && !after_operand && parser_peek_token().type == TOK_RPAREN && precedence_table[top_term][E_OPERAND] == '<') {
            get_next_token(); // '('
            get_next_token(); // ')'
            tTypeInfo unit_type = { TYPE_UNIT, TYPE_UNIT };
            tExpr* unit_expr = cg_expr_unit();
            int ue = parse_members(ctx, local, &unit_type, &unit_expr);
            if (ue != ERR_OK) {
                cg_expr_free(unit_expr);
                result = ue;
                break;
            }
            if (!stack_push_expr(&stack, unit_type, unit_expr)) {
                result = ERR_INTERNAL;
                break;
            }
            continue;
        }

        // LISTS: List(...) nebo List.range(...) na místě operandu
        if (current_token.type == TOK_KW_LIST && !after_operand && precedence_table[top_term][E_OPERAND] == '<') {
            tTypeInfo list_type;
            tExpr* list_expr = NULL;
            int le = parse_list_value(ctx, local, &list_type, &list_expr);
            if (le == ERR_OK)
                le = parse_members(ctx, local, &list_type, &list_expr);
            if (le != ERR_OK) {
                cg_expr_free(list_expr);
                result = le;
                break;
            }
            if (!stack_push_expr(&stack, list_type, list_expr)) {
                result = ERR_INTERNAL;
                break;
            }
            continue;
        }

        // if jako výraz (CONTROL) na začátku argumentu nebo uvnitř závorek, např. f(if c then 1 else 2)
        if (current_token.type == TOK_KW_IF && !after_operand && (top_sym == E_DOLLAR || top_sym == E_LPAREN)) {
            tTypeInfo if_type;
            tExpr* if_expr = NULL;
            int ie = parser_parse_if_expr(&if_type, &if_expr);
            if (ie != ERR_OK) {
                cg_expr_free(if_expr);
                result = ie;
                break;
            }
            if (!stack_push_expr(&stack, if_type, if_expr)) {
                result = ERR_INTERNAL;
                break;
            }
            continue;
        }

        // Shift nebo rovnost
        // Volání funkce ve výrazu (FUNEXP): identifikátor následovaný '('
        if (relation == '<' && input_sym == E_OPERAND && current_token.type == TOK_IDENT && parser_peek_token().type == TOK_LPAREN && !is_variable(ctx, local, current_token.value.str_val)) {
            tToken id = current_token;
            get_next_token(); // '('
            tTypeInfo call_type;
            tExpr* call_expr = NULL;
            int ce = parse_expr_function_call(ctx, local, id, &call_type, &call_expr);
            // VALMEM: členy za voláním funkce
            if (ce == ERR_OK) {
                ce = parse_members(ctx, local, &call_type, &call_expr);
            }
            if (ce != ERR_OK) {
                cg_expr_free(call_expr);
                result = ce;
                break;
            }
            if (!stack_push_expr(&stack, call_type, call_expr)) {
                result = ERR_INTERNAL;
                break;
            }
            continue;
        }
        if (relation == '<' || relation == '=') {
            if (!stack_push(&stack, table_input_sym, current_token, dummy_type)) {
                result = ERR_INTERNAL;
                break;
            }
            if (input_sym != E_NONE) {
                get_next_token();
            }
        }
        // Redukce
        else if (relation == '>') {
            // Pravidlo: E -> i
            if (stack.top->symbol == E_OPERAND && stack.top->next != NULL && stack.top->next->symbol != E_EXPR) {
                tToken operand_token = stack.top->token;
                stack_pop(&stack);

                tTypeInfo expr_type;
                // >> Ověření existence a zjištění typu operandu
                int sem_err = sem_resolve_term(ctx, local, operand_token, &expr_type);

                if (sem_err != ERR_OK) {
                    result = sem_err;
                    break;
                }

                // List stromu výrazu podle druhu operandu
                tExpr* leaf;
                switch (operand_token.type) {
                case TOK_INT_LIT:
                    leaf = cg_expr_int(operand_token.value.int_val);
                    break;
                case TOK_DOUBLE_LIT:
                    leaf = cg_expr_double(operand_token.value.double_val);
                    break;
                case TOK_STRING_LIT:
                    leaf = cg_expr_string(operand_token.value.str_val);
                    break;
                case TOK_TRUE_LIT:
                case TOK_FALSE_LIT:
                    leaf = cg_expr_bool(operand_token.type == TOK_TRUE_LIT);
                    break;
                default:
                    leaf = cg_expr_var(operand_token.value.str_val, expr_type);
                    break;
                }

                // VALMEM: členy za operandem
                sem_err = parse_members(ctx, local, &expr_type, &leaf);
                if (sem_err != ERR_OK) {
                    cg_expr_free(leaf);
                    result = sem_err;
                    break;
                }

                if (!stack_push_expr(&stack, expr_type, leaf)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }
            // Pravidlo: E -> ( E )
            else if (stack.top->symbol == E_RPAREN && stack.top->next != NULL && stack.top->next->symbol == E_EXPR && stack.top->next->next != NULL && stack.top->next->next->symbol == E_LPAREN) {

                tTypeInfo inner_type = stack.top->next->type;
                tExpr* inner_expr = take_expr(stack.top->next);

                stack_pop(&stack); // )
                stack_pop(&stack); // E
                stack_pop(&stack); // (

                // VALMEM: členy za uzávorkovaným výrazem
                int member_err = parse_members(ctx, local, &inner_type, &inner_expr);
                if (member_err != ERR_OK) {
                    cg_expr_free(inner_expr);
                    result = member_err;
                    break;
                }

                if (!stack_push_expr(&stack, inner_type, inner_expr)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }
            // Pravidlo: E -> E op E
            else if (stack.top->symbol == E_EXPR && stack.top->next != NULL && (stack.top->next->symbol == E_PLUS_MINUS || stack.top->next->symbol == E_MUL_DIV || stack.top->next->symbol == E_REL || stack.top->next->symbol == E_EQ || stack.top->next->symbol == E_LOG_AND || stack.top->next->symbol == E_LOG_OR || stack.top->next->symbol == E_CONCAT) && stack.top->next->next != NULL && stack.top->next->next->symbol == E_EXPR) {

                tTypeInfo right_type = stack.top->type;
                tToken op_token = stack.top->next->token;
                tTypeInfo left_type = stack.top->next->next->type;
                tExpr* right_expr = take_expr(stack.top);
                tExpr* left_expr = take_expr(stack.top->next->next);

                stack_pop(&stack); // E
                stack_pop(&stack); // op
                stack_pop(&stack); // E

                tTypeInfo result_type;
                // >> Sémantická kontrola kompatibility typů pro binární operátor
                int sem_err = sem_check_binary_op(ctx, op_token.type, left_type, right_type, &result_type);
                if (sem_err != ERR_OK) {
                    cg_expr_free(left_expr);
                    cg_expr_free(right_expr);
                    result = sem_err;
                    break;
                }

                tExpr* bin = cg_expr_binary(op_token.type, left_expr, right_expr, result_type);
                if (!stack_push_expr(&stack, result_type, bin)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }

            // Pravidlo: E -> - E (unární minus)
            else if (stack.top->symbol == E_EXPR && stack.top->next != NULL && stack.top->next->symbol == E_UNARY_MINUS) {

                tTypeInfo operand_type = stack.top->type;
                tTokenType op_type = stack.top->next->token.type; // TOK_MINUS nebo TOK_PLUS
                tExpr* operand_expr = take_expr(stack.top);

                stack_pop(&stack); // E
                stack_pop(&stack); // unární + / -

                tTypeInfo result_type;
                int sem_err = sem_check_unary_op(op_type, operand_type, &result_type);
                if (sem_err != ERR_OK) {
                    cg_expr_free(operand_expr);
                    result = sem_err;
                    break;
                }

                if (!stack_push_expr(&stack, result_type, cg_expr_unary(op_type, operand_expr, result_type))) {
                    result = ERR_INTERNAL;
                    break;
                }
            }

            // Pravidlo: E -> ! E (unární negace)
            else if (stack.top->symbol == E_EXPR && stack.top->next != NULL && stack.top->next->symbol == E_LOG_NOT) {

                tTypeInfo operand_type = stack.top->type;
                tExpr* operand_expr = take_expr(stack.top);
                stack_pop(&stack); // E
                stack_pop(&stack); // !

                tTypeInfo result_type;
                int sem_err = sem_check_unary_op(TOK_NOT, operand_type, &result_type);
                if (sem_err != ERR_OK) {
                    cg_expr_free(operand_expr);
                    result = sem_err;
                    break;
                }

                if (!stack_push_expr(&stack, result_type, cg_expr_unary(TOK_NOT, operand_expr, result_type))) {
                    result = ERR_INTERNAL;
                    break;
                }
            }

            else {
                result = ERR_SYNTAX;
                break;
            }
        }
        // Akceptace
        else if (relation == 'A') {
            if (stack.top->symbol == E_EXPR && stack.top->next != NULL && stack.top->next->symbol == E_DOLLAR) {
                *out_type = stack.top->type;
                if (out_expr != NULL) {
                    *out_expr = take_expr(stack.top);
                }
                result = ERR_OK;
            } else {
                result = ERR_SYNTAX;
            }
            break;
        } else {
            result = ERR_SYNTAX;
            break;
        }
    }

    stack_free(&stack);
    return result;
}