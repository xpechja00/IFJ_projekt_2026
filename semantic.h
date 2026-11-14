/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   semantic.h - rozhraní sémantické analýzy
 *
 * Autor:   xchwalo00
 */

#ifndef SEMANTIC_H
#define SEMANTIC_H

#include <stdbool.h>
#include "symtable.h"
#include "scanner.h"
#include "error.h"

// maximální hloubka zanoření bloků
#define SEM_MAX_SCOPES 256

// kontext sémantické analýzy pro udržení stavu překladu
typedef struct {
    tSymTable* global_table;
    tSymTable* scopes[SEM_MAX_SCOPES];
    int scope_top; 
    bool in_function;
    bool has_explicit_return_type;
    tTypeInfo expected_return_type;
} tSemContext;

// inicializuje sémantický kontext a vloží vestavěné funkce do globální tabulky
int sem_init(tSemContext* context, tSymTable* global_table);

// uvolní všechny zbývající lokální tabulky v kontextu
void sem_cleanup(tSemContext* context);

// otevře nový rozsah platnosti a vytvoří pro něj lokální tabulku
int sem_scope_push(tSemContext* context);

// uzavře aktuální rozsah platnosti a uvolní jeho tabulku
void sem_scope_pop(tSemContext* context);

// vyhledá proměnnou postupně od nejvnitřnějšího bloku po nejvnější
tData* sem_find_var(tSemContext* context, const char* name);

// zkontroluje že je v programu právě jedna funkce main
int sem_check_main_func(int main_function_count);

// předběžně zaregistruje hlavičku funkce z prvního průchodu (isDefined = false)
int sem_declare_function(tSymTable* global_table, const char* name, tTypeInfo return_type, tTypeInfo* params, int param_count);

// zkontroluje hlavičku funkce a uloží ji do globální tabulky symbolů
// (předběžnou deklaraci z prvního průchodu dodefinuje)
int sem_register_function(tSymTable* global_table, const char* name, tTypeInfo return_type, tTypeInfo* params, int param_count, bool is_main);

// nastaví kontext funkce a vloží parametry do nové lokální tabulky
int sem_enter_function(tSemContext* context, tTypeInfo return_type, bool has_explicit_ret, char** param_names, tTypeInfo* param_types, int param_count);

// opustí funkci a vyčistí její lokální rozsahy platnosti
void sem_leave_function(tSemContext* context);

// zkontroluje definici proměnné a její typovou kompatibilitu s inicializací
int sem_define_var(tSemContext* context, tSymTable* local_table, const char* name, bool is_mutable, bool has_explicit_type, tTypeInfo explicit_type, tTypeInfo init_type);

// zkontroluje přiřazení do existující a modifikovatelné proměnné
int sem_check_assignment(tSemContext* context, tSymTable* local_table, const char* name, tTypeInfo expr_type);

// zkontroluje volání funkce a kompatibilitu argumentů
int sem_check_function_call(tSymTable* global_table, const char* func_name, const tTypeInfo* arg_types, int arg_count, tTypeInfo* out_ret_type);

// zkontroluje typ v příkazu return vůči očekávanému typu funkce
int sem_check_return(tSemContext* context, bool has_expr, tTypeInfo expr_type);

// zkontroluje typ posledního příkazu těla funkce pro odvození nebo shodu
int sem_check_func_body_result(tSemContext* context, tTypeInfo body_type);

// zkontroluje zda je výraz v podmínce typu boolean
int sem_check_condition(tTypeInfo cond_type);

// určí typ termu ve výrazu a vyhledá případné proměnné
int sem_resolve_term(tSemContext* context, tSymTable* local_table, tToken token, tTypeInfo* out_type);

// zkontroluje unární operátor a určí výsledný typ
int sem_check_unary_op(tTokenType op, tTypeInfo operand, tTypeInfo* out_type);

// zkontroluje binární operátor a určí výsledný typ
int sem_check_binary_op(tSemContext* ctx, tTokenType op, tTypeInfo left, tTypeInfo right, tTypeInfo* out_type);

// ověří kompatibilitu dvou datových typů včetně povolené implicitní konverze
bool sem_types_compatible(tTypeInfo expected, tTypeInfo actual);

// zkontroluje člen hodnoty (výraz.id) a určí typ výsledku
int sem_check_member(tTypeInfo receiver, const char* member, tTypeInfo* out_type);

// je typ prázdný seznam List() bez známého typu prvku?
bool sem_is_untyped_list(tTypeInfo t);

// kontrola členu seznamu (LISTS); v arg_count vrátí počet argumentů typu Int,
// které člen očekává v závorkách (0 = zapisuje se bez závorek)
int sem_check_list_member(tTypeInfo receiver, const char* member, int* arg_count, tTypeInfo* out_type);

#endif
