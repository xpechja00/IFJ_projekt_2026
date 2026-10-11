/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   test_symtable.c - jednotkový test tabulky symbolů
 *
 * Test napsán s pomocí AI (Claude) (scénáře: vložení 1-10, duplicita,
 * funkce s params a bez params, hledání, 1000 vzestupných klíčů, free).
 *
 * Překlad:
 *   gcc -std=c11 -Wall -Wextra -g test_symtable.c symtable.c -o test_symtable
 * Spuštění:
 *   ./test_symtable
 *   valgrind --leak-check=full --error-exitcode=1 ./test_symtable
 */

#include <stdio.h>
#include <string.h>
#include "symtable.h"

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                  \
        }                                                                \
    } while (0)

static tData make_var(tDataType type, bool mutable_) {
    tData d;
    memset(&d, 0, sizeof(d));
    d.kind = SYM_VAR;
    d.attribute.var.typeInfo.type = type;
    d.attribute.var.isMutable = mutable_;
    return d;
}

static tData make_func(tDataType ret, int count, tTypeInfo *params) {
    tData d;
    memset(&d, 0, sizeof(d));
    d.kind = SYM_FUNC;
    d.attribute.func.returnType.type = ret;
    d.attribute.func.paramCount = count;
    d.attribute.func.params = params;
    d.attribute.func.isDefined = true;
    return d;
}

int main(void) {
    char key[32];

    tSymTable *t = symtable_create();
    CHECK(t != NULL);
    if (t == NULL) return 1;

    /* 1) vložení klíčů 1..10 vzestupně (nejhorší případ pro nevyvážený strom) */
    for (int i = 1; i <= 10; i++) {
        snprintf(key, sizeof(key), "%d", i);
        CHECK(symtable_insert(t, key, make_var(TYPE_INT, i % 2 == 0)) == INSERT_OK);
    }
    for (int i = 1; i <= 10; i++) {
        snprintf(key, sizeof(key), "%d", i);
        tData *d = symtable_search(t, key);
        CHECK(d != NULL);
        if (d != NULL) {
            CHECK(d->kind == SYM_VAR);
            CHECK(d->attribute.var.typeInfo.type == TYPE_INT);
            CHECK(d->attribute.var.isMutable == (i % 2 == 0));
        }
    }

    /* 2) duplicita nesmí přepsat původní data */
    CHECK(symtable_insert(t, "5", make_var(TYPE_STRING, true)) == INSERT_DUPLICATE);
    tData *five = symtable_search(t, "5");
    CHECK(five != NULL && five->attribute.var.typeInfo.type == TYPE_INT);

    /* 3) neexistující klíč a neplatné argumenty */
    CHECK(symtable_search(t, "neexistuje") == NULL);
    CHECK(symtable_search(t, NULL) == NULL);
    CHECK(symtable_search(NULL, "5") == NULL);
    CHECK(symtable_insert(NULL, "x", make_var(TYPE_INT, false)) == INSERT_ERROR);
    CHECK(symtable_insert(t, NULL, make_var(TYPE_INT, false)) == INSERT_ERROR);

    /* 4) funkce se dvěma parametry: tabulka musí mít vlastní kopii params */
    tTypeInfo local_params[2];
    memset(local_params, 0, sizeof(local_params));
    local_params[0].type = TYPE_INT;
    local_params[1].type = TYPE_STRING;
    CHECK(symtable_insert(t, "foo", make_func(TYPE_DOUBLE, 2, local_params)) == INSERT_OK);

    local_params[0].type = TYPE_UNIT; /* změna originálu se nesmí propsat do tabulky */
    local_params[1].type = TYPE_UNIT;

    tData *foo = symtable_search(t, "foo");
    CHECK(foo != NULL);
    if (foo != NULL) {
        CHECK(foo->kind == SYM_FUNC);
        CHECK(foo->attribute.func.returnType.type == TYPE_DOUBLE);
        CHECK(foo->attribute.func.paramCount == 2);
        CHECK(foo->attribute.func.isDefined == true);
        CHECK(foo->attribute.func.params != NULL);
        CHECK(foo->attribute.func.params != local_params);
        if (foo->attribute.func.params != NULL) {
            CHECK(foo->attribute.func.params[0].type == TYPE_INT);
            CHECK(foo->attribute.func.params[1].type == TYPE_STRING);
        }
    }

    /* 5) funkce bez parametrů: params je NULL, jiná data zůstanou */
    CHECK(symtable_insert(t, "readInt", make_func(TYPE_INT, 0, NULL)) == INSERT_OK);
    tData *ri = symtable_search(t, "readInt");
    CHECK(ri != NULL);
    if (ri != NULL) {
        CHECK(ri->kind == SYM_FUNC);
        CHECK(ri->attribute.func.paramCount == 0);
        CHECK(ri->attribute.func.params == NULL);
        CHECK(ri->attribute.func.returnType.type == TYPE_INT);
    }

    /* 6) paramCount > 0, ale params == NULL: chyba volajícího, ne pád */
    CHECK(symtable_insert(t, "bad", make_func(TYPE_INT, 1, NULL)) == INSERT_ERROR);
    CHECK(symtable_search(t, "bad") == NULL);

    /* 7) ukazatel ze search musí zůstat platný po dalších vloženích (rotace) */
    tData *keep = symtable_search(t, "3");
    CHECK(keep != NULL);
    for (int i = 100; i < 200; i++) {
        snprintf(key, sizeof(key), "k%d", i);
        CHECK(symtable_insert(t, key, make_var(TYPE_INT, false)) == INSERT_OK);
    }
    CHECK(keep == symtable_search(t, "3"));

    symtable_free(t);

    /* 8) velký vzestupný vstup: 1000 klíčů, vše musí jít najít */
    t = symtable_create();
    CHECK(t != NULL);
    if (t == NULL) return 1;
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "id%04d", i);
        CHECK(symtable_insert(t, key, make_var(TYPE_INT, false)) == INSERT_OK);
    }
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "id%04d", i);
        CHECK(symtable_search(t, key) != NULL);
    }
    CHECK(symtable_search(t, "id1000") == NULL);
    symtable_free(t);

    /* 9) free(NULL) a free prázdné tabulky nesmí spadnout */
    symtable_free(NULL);
    t = symtable_create();
    symtable_free(t);

    if (failures == 0) {
        printf("OK: všechny testy prošly\n");
        return 0;
    }
    printf("CHYB: %d\n", failures);
    return 1;
}