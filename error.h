/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   error.h - rozhraní pro chybové kódy
 *
 * Autor: xpechja00
 */

#ifndef ERROR_H
#define ERROR_H

typedef enum {
    ERR_OK = 0,
    ERR_LEX = 1,
    ERR_SYNTAX = 2,
    ERR_UNDEF = 3,
    ERR_REDEF = 4,
    ERR_ARGS = 5,
    ERR_TYPE = 6,
    ERR_ASSIGN = 7,
    ERR_OTHER = 10,
    ERR_INTERNAL = 99
} tErrorCode;

void error_exit(tErrorCode code, const char* fmt, ...);
#endif