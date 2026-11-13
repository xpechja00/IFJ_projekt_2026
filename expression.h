/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   expression.h - rozhraní precedenční syntaktické analýzy
 * Autor: xdundrm00
 */ 
#ifndef EXPRESSION_H 
#define EXPRESSION_H

#include "scanner.h" 
#include "error.h"
#include "symtable.h"
#include "semantic.h"

int parse_expression_psa(tSemContext* ctx, tSymTable* local_table, tTypeInfo* out_type);

#endif