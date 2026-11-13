/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   semantic.c - implementace sémantické kontroly
 *
 * Autor:   xchwalo00
 */

#include "semantic.h"
#include <stdlib.h>
#include <string.h>

// konstruktor pro tTypeInfo
static tTypeInfo make_type(tDataType t)
{
    tTypeInfo info;
    info.type = t;
    info.innerType = TYPE_UNIT;
    return info;
}

// pomocná funkce pro ověření typu proměnné nebo funkce
static bool is_valid_value_type(tTypeInfo t)
{
    return (t.type == TYPE_INT || t.type == TYPE_DOUBLE || t.type == TYPE_STRING || t.type == TYPE_UNIT);
}

// ověření kompatibility datových typů včetně konverze
bool sem_types_compatible(tTypeInfo expected, tTypeInfo actual)
{
    if ((is_valid_value_type(expected) == false) || (is_valid_value_type(actual) == false)) {
        return false;
    }
    if (expected.type == actual.type) {
        return true;
    }
    // rozšíření Int na Double
    if (expected.type == TYPE_DOUBLE && actual.type == TYPE_INT) {
        return true;
    }
    return false;
}

// pomocná funkce na vložení vestavěné funkce do tabulky symbolů
static int insert_builtin(tSymTable* global_table, const char* name, tDataType ret_type, int param_count, tDataType p1, tDataType p2, tDataType p3)
{
    // pole pro parametry
    tTypeInfo params[3];
    params[0] = make_type(p1);
    params[1] = make_type(p2);
    params[2] = make_type(p3);

    // přidaní atributů do struktury data  
    tData data;
    data.kind = SYM_FUNC;
    data.attribute.func.returnType = make_type(ret_type);
    data.attribute.func.paramCount = param_count;
    data.attribute.func.params = (param_count > 0) ? params : NULL;
    data.attribute.func.isDefined = true;

    // vložení do globální tabulky symbolů
    tInsertResult res = symtable_insert(global_table, name, data);
    if (res == INSERT_ERROR) {
        return ERR_INTERNAL;
    }
    return ERR_OK;
}

// inicializace sémantického kontextu a registrace vestavěných funkcí
int sem_init(tSemContext* context, tSymTable* global_table)
{
    if (context == NULL || global_table == NULL) {
        return ERR_INTERNAL;
    }

    context->global_table = global_table;
    context->scope_top = -1;
    context->in_function = false;
    context->has_explicit_return_type = false;
    context->expected_return_type = make_type(TYPE_UNIT);

    for (int a = 0; a < SEM_MAX_SCOPES; a++) {
        context->scopes[a] = NULL;
    }

    // registrace vestavěných funkcí 
    int err = ERR_OK;
    if ((err = insert_builtin(global_table, "print", TYPE_UNIT, 1, TYPE_UNIT, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "readLine", TYPE_STRING, 0, TYPE_UNIT, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "readInt", TYPE_INT, 0, TYPE_UNIT, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "readDouble", TYPE_DOUBLE, 0, TYPE_UNIT, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "toStr", TYPE_STRING, 1, TYPE_UNIT, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "toInt", TYPE_INT, 1, TYPE_DOUBLE, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "strLength", TYPE_INT, 1, TYPE_STRING, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "subStr", TYPE_STRING, 3, TYPE_STRING, TYPE_INT, TYPE_INT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "ord", TYPE_INT, 2, TYPE_STRING, TYPE_INT, TYPE_UNIT)) != ERR_OK) return err;
    if ((err = insert_builtin(global_table, "chr", TYPE_STRING, 1, TYPE_INT, TYPE_UNIT, TYPE_UNIT)) != ERR_OK) return err;

    return ERR_OK;
}

// odstraní aktuální lokální tabulku symbolů z vrcholu zásobníku a uvolní paměť
void sem_scope_pop(tSemContext* context)
{
    if (context == NULL || context->scope_top < 0) {
        return;
    }
    symtable_free(context->scopes[context->scope_top]);
    context->scopes[context->scope_top] = NULL;
    context->scope_top--;
}

// uvolnění paměti lokálních tabulek symbolů a reset stavu kontextu
void sem_cleanup(tSemContext* context)
{
    if (context == NULL) {
        return;
    }
    while (context->scope_top >= 0) {
        sem_scope_pop(context);
    }
    context->in_function = false;
}

// vytvoření nové lokální tabulky symbolů a její vložení na vrchol zásobníku
int sem_scope_push(tSemContext* context)
{
    if (context == NULL) {
        return ERR_INTERNAL;
    }
    if (context->scope_top + 1 >= SEM_MAX_SCOPES) {
        return ERR_INTERNAL;
    }
    tSymTable* new_table = symtable_create();
    if (new_table == NULL) {
        return ERR_INTERNAL;
    }
    context->scopes[++context->scope_top] = new_table;
    return ERR_OK;
}

// vyhledání proměnné postupně od nejvnitřnějšího bloku po nejvnější
tData* sem_find_var(tSemContext* context, const char* name)
{
    if (context == NULL || name == NULL) {
        return NULL;
    }
    
    for (int a = context->scope_top; a >= 0; a--) {
        tData* found = symtable_search(context->scopes[a], name);
        if (found != NULL && found->kind == SYM_VAR) {
            return found;
        }
    }
    return NULL;
}

// ověření jedné funkce main
int sem_check_main_func(int main_function_count)
{
    if (main_function_count == 0) {
        return ERR_UNDEF;
    }
    if (main_function_count > 1) {
        return ERR_REDEF;
    }
    return ERR_OK;
}

// kontrola hlavičky funkcí a uložení do globální tabulky symbolů
int sem_register_function(tSymTable* global_table, const char* name, tTypeInfo return_type, tTypeInfo* params, int param_count, bool is_main)
{
    if (global_table == NULL || name == NULL) {
        return ERR_INTERNAL;
    }

    // kontrola redefinice funkce
    tData* existing = symtable_search(global_table, name);
    if (existing != NULL) {
        if ((existing->kind != SYM_FUNC) || (existing->attribute.func.isDefined == true)) {
            return ERR_REDEF; 
        }
    }

    // kontrola main (nula parametrů a typ unit)
    if (is_main == true) {
        if (param_count > 0) {
            return ERR_ARGS; 
        }
        if (return_type.type != TYPE_UNIT) {
            return ERR_TYPE; 
        }
    }

    // kontrola platnosti typů parametrů a návratového typu 
    if (is_valid_value_type(return_type) == false) {
        return ERR_TYPE;
    }
    for (int a = 0; a < param_count; a++) {
        if ((params == NULL) || (is_valid_value_type(params[a]) == false)) {
            return ERR_TYPE;
        }
    }

    // uložení dat o funkci do globální tabulky symbolů
    tData f_data;
    f_data.kind = SYM_FUNC;
    f_data.attribute.func.returnType = return_type;
    f_data.attribute.func.paramCount = param_count;
    f_data.attribute.func.params = params;
    f_data.attribute.func.isDefined = true;

    tInsertResult res = symtable_insert(global_table, name, f_data);
    if (res == INSERT_DUPLICATE) {
        return ERR_REDEF;
    } else if (res == INSERT_ERROR) {
        return ERR_INTERNAL;
    }

    return ERR_OK;
}

// příprava kontextu před tělem funkce a vložení parametrů
int sem_enter_function(tSemContext* context, tTypeInfo return_type, bool has_explicit_ret, char** param_names, tTypeInfo* param_types, int param_count)
{
    if (context == NULL) {
        return ERR_INTERNAL;
    }

    sem_cleanup(context);

    int err = sem_scope_push(context);
    if (err != ERR_OK) {
        return err;
    }

    context->in_function = true;
    context->has_explicit_return_type = has_explicit_ret;
    context->expected_return_type = return_type;

    // vložení parametrů do lokálního scope funkce
    tSymTable* func_scope = context->scopes[context->scope_top];
    for (int a = 0; a < param_count; a++) {
        if (param_names == NULL || param_names[a] == NULL || param_types == NULL) {
            return ERR_INTERNAL;
        }
        if (is_valid_value_type(param_types[a]) == false) {
            return ERR_TYPE;
        }

        tData p_data;
        p_data.kind = SYM_VAR;
        p_data.attribute.var.isMutable = false;
        p_data.attribute.var.typeInfo = param_types[a];

        tInsertResult res = symtable_insert(func_scope, param_names[a], p_data);
        if (res == INSERT_DUPLICATE) {
            return ERR_REDEF;
        } else if (res == INSERT_ERROR) {
            return ERR_INTERNAL;
        }
    }

    return ERR_OK;
}

// úklid po zpracování těla funkce
void sem_leave_function(tSemContext* context)
{
    sem_cleanup(context);
}

// definice proměnné a určení jejího typu
int sem_define_var(tSemContext* context, tSymTable* local_table, const char* name, bool is_mutable, bool has_explicit_type, tTypeInfo explicit_type, tTypeInfo init_type)
{
    if (name == NULL) {
        return ERR_INTERNAL;
    }

    // určení cílové tabulky
    tSymTable* target_table = local_table;
    if (context != NULL && context->scope_top >= 0) {
        target_table = context->scopes[context->scope_top];
    }
    if (target_table == NULL) {
        return ERR_INTERNAL;
    }

    // výsledek inicializace nesmí být nepovolený typ
    if (is_valid_value_type(init_type) == false) {
        return ERR_TYPE;
    }

    tTypeInfo final_type;
    if (has_explicit_type == true) {
        if (is_valid_value_type(explicit_type) == false) {
            return ERR_TYPE;
        }
        if (sem_types_compatible(explicit_type, init_type) == false) {
            return ERR_TYPE;
        }
        final_type = explicit_type;
    } else {
        // odvození typu z inicializace
        final_type = init_type;
    }

    // kontrola redefinice v aktuálním bloku
    tData v_data;
    v_data.kind = SYM_VAR;
    v_data.attribute.var.isMutable = is_mutable;
    v_data.attribute.var.typeInfo = final_type;

    tInsertResult res = symtable_insert(target_table, name, v_data);
    if (res == INSERT_DUPLICATE) {
        return ERR_REDEF;
    } else if (res == INSERT_ERROR) {
        return ERR_INTERNAL;
    }

    return ERR_OK;
}

// kontrola přiřazení do proměnné
int sem_check_assignment(tSemContext* context, tSymTable* local_table, const char* name, tTypeInfo expr_type)
{
    if (name == NULL) {
        return ERR_INTERNAL;
    }

    tData* var_entry = NULL;
    if (context != NULL && context->scope_top >= 0) {
        var_entry = sem_find_var(context, name);
    } else if (local_table != NULL) {
        var_entry = symtable_search(local_table, name);
    }

    // proměnná musí existovat
    if (var_entry == NULL || var_entry->kind != SYM_VAR) {
        return ERR_UNDEF;
    }

    // proměnná musí být modifikovatelná
    if (var_entry->attribute.var.isMutable == false) {
        return ERR_ASSIGN;
    }

    // kompatibilita typů
    if (sem_types_compatible(var_entry->attribute.var.typeInfo, expr_type) == false) {
        return ERR_TYPE;
    }

    return ERR_OK;
}

// kontrola volání funkce a jejich argumentů
int sem_check_function_call(tSymTable* global_table, const char* func_name, const tTypeInfo* arg_types, int arg_count, tTypeInfo* out_ret_type)
{
    if (global_table == NULL || func_name == NULL || arg_count < 0) {
        return ERR_INTERNAL;
    }

    // ošetření vestavěných funkcí
    if (strcmp(func_name, "print") == 0 || strcmp(func_name, "toStr") == 0) {
        if (arg_count != 1 || arg_types == NULL) {
            return ERR_ARGS;
        }
        if (is_valid_value_type(arg_types[0]) == false) {
            return ERR_ARGS;
        }
        if (out_ret_type != NULL) {
            *out_ret_type = make_type(strcmp(func_name, "print") == 0 ? TYPE_UNIT : TYPE_STRING);
        }
        return ERR_OK;
    }

    // vyhledání funkce v globální tabulce symbolů
    tData* f_entry = symtable_search(global_table, func_name);
    if (f_entry == NULL || f_entry->kind != SYM_FUNC) {
        return ERR_UNDEF;
    }

    // kontrola počtu argumentů
    if (f_entry->attribute.func.paramCount != arg_count) {
        return ERR_ARGS;
    }

    // kontrola typů argumentů
    for (int a = 0; a < arg_count; a++) {
        if (arg_types == NULL || sem_types_compatible(f_entry->attribute.func.params[a], arg_types[a]) == false) {
            return ERR_ARGS;
        }
    }

    if (out_ret_type != NULL) {
        *out_ret_type = f_entry->attribute.func.returnType;
    }

    return ERR_OK;
}

// kontrola příkazu return
int sem_check_return(tSemContext* context, bool has_expr, tTypeInfo expr_type)
{
    if (context == NULL || context->in_function == false) {
        return ERR_OTHER;
    }

    // návratový typ musí být explicitní pokud se používá return
    if (context->has_explicit_return_type == false) {
        return ERR_TYPE;
    }

    // samostatný return
    if (has_expr == false) {
        if (context->expected_return_type.type != TYPE_UNIT) {
            return ERR_TYPE;
        }
        return ERR_OK;
    }

    // výraz v return musí mít platný typ
    if (is_valid_value_type(expr_type) == false) {
        return ERR_TYPE;
    }

    // vyhodnocení a zahození návratové hodnoty u unit
    if (context->expected_return_type.type == TYPE_UNIT) {
        return ERR_OK;
    }

    // kompatibilita návratové hodnoty
    if (sem_types_compatible(context->expected_return_type, expr_type) == false) {
        return ERR_TYPE;
    }

    return ERR_OK;
}

// kontrola koncového výrazu těla funkce
int sem_check_func_body_result(tSemContext* context, tTypeInfo body_type)
{
    if (context == NULL || context->in_function == false) {
        return ERR_INTERNAL;
    }

    if (is_valid_value_type(body_type) == false) {
        return ERR_TYPE;
    }

    if (context->has_explicit_return_type == true) {
        if (context->expected_return_type.type == TYPE_UNIT) {
            return ERR_OK;
        }
        if (sem_types_compatible(context->expected_return_type, body_type) == false) {
            return ERR_TYPE;
        }
    } else {
        // odvození typu z výsledku těla
        context->expected_return_type = body_type;
    }

    return ERR_OK;
}

// kontrola podmínky
int sem_check_condition(tTypeInfo cond_type)
{
    // podmínka musí být boolean
    if (cond_type.type != TYPE_BOOLEAN) {
        return ERR_TYPE;
    }
    return ERR_OK;
}

// vyhodnocení termu jako je literál nebo proměnná
int sem_resolve_term(tSemContext* context, tSymTable* local_table, tToken token, tTypeInfo* out_type)
{
    if (out_type == NULL) {
        return ERR_INTERNAL;
    }

    switch (token.type) {
    case TOK_INT_LIT:
        *out_type = make_type(TYPE_INT);
        return ERR_OK;

    case TOK_DOUBLE_LIT:
        *out_type = make_type(TYPE_DOUBLE);
        return ERR_OK;

    case TOK_STRING_LIT:
        *out_type = make_type(TYPE_STRING);
        return ERR_OK;

    case TOK_IDENT: {
        if (token.value.str_val == NULL) {
            return ERR_INTERNAL;
        }
        tData* var = NULL;
        if (context != NULL && context->scope_top >= 0) {
            var = sem_find_var(context, token.value.str_val);
        } else if (local_table != NULL) {
            var = symtable_search(local_table, token.value.str_val);
        }
        if (var == NULL || var->kind != SYM_VAR) {
            return ERR_UNDEF;
        }
        *out_type = var->attribute.var.typeInfo;
        return ERR_OK;
    }

    default:
        return ERR_SYNTAX;
    }
}

// kontrola unárních operátorů
int sem_check_unary_op(tTokenType op, tTypeInfo operand, tTypeInfo* out_type)
{
    if (out_type == NULL) {
        return ERR_INTERNAL;
    }

    // unární operátory jen na čísla
    if (op == TOK_MINUS || op == TOK_PLUS) {
        if (operand.type == TYPE_INT || operand.type == TYPE_DOUBLE) {
            *out_type = operand;
            return ERR_OK;
        }
        return ERR_TYPE;
    }

    return ERR_TYPE;
}

// kontrola binárních operátorů
int sem_check_binary_op(tTokenType op, tTypeInfo left, tTypeInfo right, tTypeInfo* out_type)
{
    if (out_type == NULL) {
        return ERR_INTERNAL;
    }

    if (is_valid_value_type(left) == false || is_valid_value_type(right) == false) {
        return ERR_TYPE;
    }

    bool left_num = (left.type == TYPE_INT || left.type == TYPE_DOUBLE);
    bool right_num = (right.type == TYPE_INT || right.type == TYPE_DOUBLE);

    switch (op) {
    case TOK_PLUS:
        if (left_num == true && right_num == true) {
            *out_type = make_type((left.type == TYPE_DOUBLE || right.type == TYPE_DOUBLE) ? TYPE_DOUBLE : TYPE_INT);
            return ERR_OK;
        }
        if (left.type == TYPE_STRING && right.type == TYPE_STRING) {
            *out_type = make_type(TYPE_STRING);
            return ERR_OK;
        }
        return ERR_TYPE;

    case TOK_MINUS:
    case TOK_STAR:
    case TOK_SLASH:
        if (left_num == true && right_num == true) {
            *out_type = make_type((left.type == TYPE_DOUBLE || right.type == TYPE_DOUBLE) ? TYPE_DOUBLE : TYPE_INT);
            return ERR_OK;
        }
        return ERR_TYPE;

    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        if ((left_num == true && right_num == true) || (left.type == TYPE_STRING && right.type == TYPE_STRING)) {
            *out_type = make_type(TYPE_BOOLEAN);
            return ERR_OK;
        }
        return ERR_TYPE;

    case TOK_EQ:
    case TOK_NEQ:
        if ((left_num == true && right_num == true) || (left.type == right.type)) {
            *out_type = make_type(TYPE_BOOLEAN);
            return ERR_OK;
        }
        return ERR_TYPE;

    default:
        return ERR_TYPE;
    }
}