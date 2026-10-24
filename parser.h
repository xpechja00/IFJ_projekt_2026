/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   parser.h - hlavičkový soubor pro syntaktickou analýzu
 *
 * Autor: xdundrm00
 *
 */

#ifndef PARSER_H
#define PARSER_H

#include <stdio.h>
#include <stdbool.h>

// Zahrnutí sdílených modulů překladače
#include "scanner.h"
#include "error.h"
#include "symtable.h"

/**
 * @brief Hlavní spouštěcí funkce syntaktického analyzátoru.
 * 
 * Zpracovává posloupnost tokenů ze scanneru pomocí metody
 * rekurzivního sestupu. Provádí syntaktickou a základní
 * sémantickou kontrolu za pomoci tabulky symbolů.
 * Při nalezení chyby přímo volá error_exit.
 * 
 * @return int Návratový kód (0 pro úspěch - ERR_OK).
 */
int parse();

#endif 