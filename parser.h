/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   pasrer.h - rozhraní syntaktického analyzátoru
 *
 * Autor: xdundrm00
 *
 */
#ifndef PARSER_H
#define PARSER_H

#include "symtable.h"
#include "scanner.h" 

// Hlavní funkce
int parse(tSymTable* global_symtable);

extern tToken current_token;

void get_next_token(void);

#endif 