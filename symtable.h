//autor xpechja00

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

// struktura na uchování informaci o typu
typedef struct {
    tDataType type;
    tDataType innerType; // použije se když type == TYPE_LIST
} tTypeInfo;

// rozlišení funkce a proměnné
typedef enum {
    SYM_VAR,
    SYM_FUNC
} tSymbolKind;

// ukládaná data pro proměnné
typedef struct {
    tTypeInfo typeInfo;
    bool isMutable; // true => var, false => val
} tVarData;

// ukládaná data pro funkce
typedef struct {
    tTypeInfo returnType;
    int paramCount;
    tTypeInfo *params; 
    bool isDefined;    // true, pokud už bylo zpracováno tělo funkce
} tFuncData;

// hlavní datová struktura
typedef struct {
    tSymbolKind kind;
    union {
        tVarData var;
        tFuncData func;
    } attribute;
} tData;

// uzel
typedef struct node {
    char *key;           
    tData data;          
    int height;          
    struct node *left;   
    struct node *right;  
} tNode;

// tabulka symbolů
typedef struct {
    tNode *root;   
} tSymTable;

void symtable_init(tSymTable *table);

tData *symtable_search(tSymTable *table, const char *key);

bool symtable_insert(tSymTable *table, const char *key, tData data);

void symtable_free(tSymTable *table);

#endif // SYMTABLE_H