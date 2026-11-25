/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   main.c
 *
 * Autor: xpechja00
 */
#include <stddef.h>

#include "error.h"
#include "parser.h"
#include "symtable.h"

int main(void)
{
    tSymTable* global_symtable;
    global_symtable = symtable_create();

    if (global_symtable == NULL) {
        return ERR_INTERNAL;
    }
    int err_code = parse(global_symtable);
    symtable_free(global_symtable);
    return err_code;
}