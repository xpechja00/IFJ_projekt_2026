/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   parser.h - rozhraní syntaktického analyzátoru
 *
 * Autor: xdundrm00
 *
 */
#ifndef PARSER_H
#define PARSER_H

#include "scanner.h"
#include "symtable.h"
#include <stdbool.h>

// Hlavní funkce: načte tokeny, provede oba průchody a vrátí chybový kód (tErrorCode)
int parse(tSymTable* global_symtable);

// Aktuální token (sdílený s precedenční analýzou v expression.c)
extern tToken current_token;

// Posun na další token
void get_next_token(void);

// Další token bez jeho spotřebování
tToken parser_peek_token(void);

// Před kontrolou volání: zaznamená hranu grafu volání a u funkce bez explicitního
// návratového typu, jejíž tělo ještě nebylo zpracováno, jej hned zanalyzuje
// (vnořená analýza). Vrací ERR_TYPE při rekurzi přes funkci bez návratového typu.
int parser_resolve_function(const char* name);

// Zpracuje if jako výraz (CONTROL) uvnitř precedenční analýzy, typ vrátí v out_type
int parser_parse_if_expr(tTypeInfo* out_type);

// true, pokud se má generovat kód (false během vnořené analýzy, která jen kontroluje)
bool parser_codegen_enabled(void);

#endif
