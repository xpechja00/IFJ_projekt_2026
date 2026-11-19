/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   symtable.h - tabulka symbolů (vv-BVS) jako ADT
 *
 * Autor: xpechja00
 */

#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdbool.h>

// datové typy
typedef enum {
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_UNIT,
    TYPE_BOOLEAN,
    TYPE_LIST
} tDataType;

// informace o typu
typedef struct {
    tDataType type;
    tDataType innerType; // platné jen pro type == TYPE_LIST
} tTypeInfo;

// rozlišení funkce a proměnné
typedef enum { SYM_VAR,
    SYM_FUNC } tSymbolKind;

// data proměnné
typedef struct {
    tTypeInfo typeInfo;
    bool isMutable; // true => var, false => val
} tVarData;

// data funkce
typedef struct {
    tTypeInfo returnType;
    int paramCount;
    tTypeInfo* params; // pole délky paramCount; tabulka si při vložení vytváří
                       // vlastní kopii, volající si své pole spravuje sám
    bool isDefined; // true, pokud už bylo zpracováno tělo funkce
} tFuncData;

// záznam uložený v tabulce
typedef struct {
    tSymbolKind kind;
    union {
        tVarData var;
        tFuncData func;
    } attribute;
} tData;

// neúplný typ - definice struktury je skrytá v symtable.c
typedef struct tSymTable tSymTable;

// výsledek vložení do tabulky
typedef enum { INSERT_OK,
    INSERT_DUPLICATE,
    INSERT_ERROR } tInsertResult;

// vytvoří prázdnou tabulku; při chybě alokace vrací NULL
tSymTable* symtable_create(void);

// vrátí ukazatel na data pro klíč, nebo NULL, pokud klíč neexistuje
tData* symtable_search(tSymTable* table, const char* key);

// vloží klíč a data; false při duplicitě nebo chybě alokace
tInsertResult symtable_insert(tSymTable* table, const char* key, tData data);

// uvolní celou tabulku (uzly, klíče, případně params)
void symtable_free(tSymTable* table);

#endif // SYMTABLE_H