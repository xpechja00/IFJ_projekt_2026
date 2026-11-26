/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   main.c - vstupní bod překladače
 */
#include "codegen.h"
#include "error.h"
#include "parser.h"
#include "symtable.h"
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    tSymTable* global_symtable = symtable_create();
    if (global_symtable == NULL) {
        return ERR_INTERNAL;
    }
    cg_init();
    int err_code = parse(global_symtable);
    if (err_code == ERR_OK) {
        cg_emit_program(stdout);
    }
    cg_cleanup();
    symtable_free(global_symtable);
    return err_code;
}