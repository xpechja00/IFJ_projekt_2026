/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Kod:   codegen.h - rozhraní generátoru cílového kódu IFJcode26
 */

#ifndef CODEGEN_H
#define CODEGEN_H

#include "scanner.h"
#include "symtable.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    EXPR_INT,
    EXPR_DOUBLE,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_UNIT,
    EXPR_VAR,
    EXPR_TEMP_VAR,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_CALL,
    EXPR_MEMBER,
    EXPR_LIST_LIT,
    EXPR_LIST_INDEX,
    EXPR_RANGE
} tExprKind;

typedef struct tExpr {
    tExprKind kind;
    tTypeInfo type;
    union {
        int32_t int_val;
        double double_val;
        char* str_val;
        bool bool_val;
        struct {
            tTokenType op;
            struct tExpr* operand;
        } unary;
        struct {
            tTokenType op;
            struct tExpr* left;
            struct tExpr* right;
        } binary;
        struct {
            char* name;
            struct tExpr** args;
            int arg_count;
        } call;
        struct {
            struct tExpr* receiver;
            char* member;
            struct tExpr** args;
            int arg_count;
        } member;
        struct {
            struct tExpr** elems;
            int count;
        } list_lit;
        struct {
            struct tExpr* list_expr;
            struct tExpr* index_expr;
        } list_index;
        struct {
            struct tExpr* start;
            struct tExpr* end;
            struct tExpr* step; // NULL => krok 1
            bool inclusive;
        } range;
    } data;
} tExpr;

// Konstruktory a uvolnění AST uzlů výrazů
tExpr* cg_expr_int(int32_t val);
tExpr* cg_expr_double(double val);
tExpr* cg_expr_string(const char* val);
tExpr* cg_expr_bool(bool val);
tExpr* cg_expr_unit(void);
tExpr* cg_expr_var(const char* name, tTypeInfo type);
tExpr* cg_expr_temp_var(const char* lf_name, tTypeInfo type);
tExpr* cg_expr_unary(tTokenType op, tExpr* operand, tTypeInfo result_type);
tExpr* cg_expr_binary(tTokenType op, tExpr* left, tExpr* right, tTypeInfo result_type);
tExpr* cg_expr_call(const char* name, tExpr** args, int arg_count, tTypeInfo ret_type);
tExpr* cg_expr_member(tExpr* receiver, const char* member, tExpr** args, int arg_count, tTypeInfo res_type);
tExpr* cg_expr_list_lit(tExpr** elems, int count, tTypeInfo list_type);
tExpr* cg_expr_list_index(tExpr* list_expr, tExpr* index_expr, tTypeInfo elem_type);
tExpr* cg_expr_range(tExpr* start, tExpr* end, tExpr* step, bool inclusive);
void cg_expr_free(tExpr* expr);

// Životní cyklus generátoru
int cg_init(void);
void cg_cleanup(void);
void cg_set_main(const char* main_func_name);
void cg_emit_program(FILE* out);

// Funkce a rozsahy platnosti (scopes)
void cg_func_begin(const char* name, char** param_names, int param_count);
void cg_func_end(tTypeInfo ret_type, tTypeInfo last_body_type, bool ended_with_return);
void cg_scope_push(void);
void cg_scope_pop(void);

// Definice a vyhledání proměnných v aktuální funkci (vrací např. "LF@x$0")
const char* cg_define_var(const char* name, tTypeInfo type);
const char* cg_find_var(const char* name, tTypeInfo* out_type);
const char* cg_new_temp(void);
int cg_new_label_id(void);

// Generování výrazů a příkazů
void cg_emit_expr_into(tExpr* expr, const char* dest_var);
void cg_emit_var_init(const char* name, tTypeInfo var_type, tExpr* init_expr);
void cg_emit_assign(const char* name, tExpr* rhs_expr);
void cg_emit_return(bool has_expr, tExpr* expr, tTypeInfo expected_ret_type);
void cg_emit_widen_if_needed(const char* var_name, tTypeInfo expected, tTypeInfo actual);

// Řídicí struktury: if
typedef struct {
    int id;
    char* res_var;
    bool has_else;
} tCgIfState;

tCgIfState cg_if_begin(tExpr* cond, bool need_result);
void cg_if_else(tCgIfState* state);
void cg_if_end(tCgIfState* state, bool need_result, tTypeInfo res_type, tTypeInfo then_type, tTypeInfo else_type);

// Řídicí struktury: while
typedef struct {
    int id;
} tCgWhileState;

tCgWhileState cg_while_begin(void);
void cg_while_cond(tCgWhileState* state, tExpr* cond);
void cg_while_end(tCgWhileState* state);

// Řídicí struktury: for
typedef struct {
    int id;
    bool over_list;
    bool inclusive;
    char* v_iter;
    char* v_end;
    char* v_step;
    char* v_list;
    char* v_idx;
    char* v_len;
} tCgForState;

tCgForState cg_for_range_begin(const char* var_name, tExpr* start, tExpr* end, tExpr* step, bool inclusive);
tCgForState cg_for_list_begin(const char* var_name, tTypeInfo elem_type, tExpr* list_expr);
void cg_for_guard(tCgForState* state, tExpr* guard_expr);
void cg_for_body_begin(tCgForState* state);
void cg_for_end(tCgForState* state);

#endif // CODEGEN_H