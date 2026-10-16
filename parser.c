/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   parser.c - syntaktický analyzátor (rekurzivní sestup)
 *
 * Autor: xdundrm00
 */

#include "parser.h"
#include "scanner.h"
#include "error.h"
#include "symtable.h"
#include <string.h>

// Aktuálně zpracovávaný token
static tToken current_token;

// Deklarace interních funkcí pro rekurzivní sestup
int parse_program();
int parse_statement();
int parse_expression() {
    // Zatím umíme přečíst jen jeden samostatný token (literál nebo proměnnou)
    if (current_token.type == TOK_INT_LIT || 
        current_token.type == TOK_DOUBLE_LIT || 
        current_token.type == TOK_STRING_LIT || 
        current_token.type == TOK_TRUE_LIT || 
        current_token.type == TOK_FALSE_LIT || 
        current_token.type == TOK_IDENT) {
        
        current_token = scanner_next();
    } else {
        error_exit(ERR_SYNTAX, "Očekáván výraz (hodnota nebo proměnná)");
    }

    // TODO: Zde později přidáme cyklus a zásobník pro řešení operátorů (+, -, *, /)
    
    return ERR_OK;
}
int parse_list();
int parse_control_flow();
int parse_function_expression();

// Hlavní funkce
int parse() {
    current_token = scanner_next();
    return parse_program();
}

int parse_program() {
    // Prolog
    if (current_token.type != TOK_KW_IMPORT) 
        error_exit(ERR_SYNTAX, "Očekáváno klíčové slovo 'import'");
    current_token = scanner_next();
    
    if (current_token.type != TOK_IDENT || strcmp(current_token.value.str_val, "ifj26") != 0) 
        error_exit(ERR_SYNTAX, "Očekáván identifikátor 'ifj26'");
    current_token = scanner_next();
    
    if (current_token.type != TOK_DOT) 
        error_exit(ERR_SYNTAX, "Očekávána tečka za 'ifj26'");
    current_token = scanner_next();
    
    if (current_token.type != TOK_STAR) 
        error_exit(ERR_SYNTAX, "Očekávána hvězdička v prologu");
    current_token = scanner_next();
    
    // Hlavní cyklus zpracování na nejvyšší úrovni
    while (current_token.type != TOK_EOF) {
        if (current_token.type == TOK_EOL) {
            current_token = scanner_next();
            continue;
        }

        if (current_token.type == TOK_KW_DEF) {
            parse_function_expression();
        } else if (current_token.type == TOK_KW_VAR || current_token.type == TOK_KW_VAL) {
            parse_statement();
        } else {
            error_exit(ERR_SYNTAX, "Neočekávaný token na nejvyšší úrovni programu");
        }
    }
    
    return ERR_OK; 
}

// Příkazy a struktury
int parse_statement() {
    // 1. Zpracování definice proměnné (var / val)
    if (current_token.type == TOK_KW_VAR || current_token.type == TOK_KW_VAL) {
        current_token = scanner_next();

        if (current_token.type != TOK_IDENT) error_exit(ERR_SYNTAX, "Očekáván název proměnné");
        current_token = scanner_next();

        if (current_token.type == TOK_COLON) {
            current_token = scanner_next();
            if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
                current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
                current_token.type != TOK_KW_BOOLEAN) {
                error_exit(ERR_SYNTAX, "Očekáván datový typ proměnné");
            }
            current_token = scanner_next();
        } else {
            error_exit(ERR_SYNTAX, "Očekávána ':' a datový typ");
        }

        if (current_token.type != TOK_ASSIGN) error_exit(ERR_SYNTAX, "Očekáváno '='");
        current_token = scanner_next();

        parse_expression();

        if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
            error_exit(ERR_SYNTAX, "Očekáván konec řádku za definicí");
        }
        if (current_token.type == TOK_EOL) current_token = scanner_next();
        
        return ERR_OK;
    }

    // 2. Zpracování podmínky 
    else if (current_token.type == TOK_KW_IF) {
        current_token = scanner_next();
        
        // Zpracování podmínky 
        if (current_token.type != TOK_LPAREN) error_exit(ERR_SYNTAX, "Očekávána '(' za 'if'");
        current_token = scanner_next();
        parse_expression();
        if (current_token.type != TOK_RPAREN) error_exit(ERR_SYNTAX, "Očekávána ')' za podmínkou");
        current_token = scanner_next();

        if (current_token.type != TOK_KW_THEN) error_exit(ERR_SYNTAX, "Očekáváno 'then'");
        current_token = scanner_next();

        if (current_token.type != TOK_EOL) error_exit(ERR_SYNTAX, "Očekáván konec řádku za 'then'");
        current_token = scanner_next();

        if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'then'");
        current_token = scanner_next();

        // Tělo if bloku
        while (current_token.type != TOK_DEDENT) {
            if (current_token.type == TOK_EOL) { current_token = scanner_next(); continue; }
            parse_statement();
        }
        current_token = scanner_next(); 

        // Else
        if (current_token.type == TOK_KW_ELSE) {
            current_token = scanner_next();
            if (current_token.type != TOK_EOL) error_exit(ERR_SYNTAX, "Očekáván konec řádku za 'else'");
            current_token = scanner_next();
            
            if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'else'");
            current_token = scanner_next();

            // Tělo else bloku
            while (current_token.type != TOK_DEDENT) {
                if (current_token.type == TOK_EOL) { current_token = scanner_next(); continue; }
                parse_statement();
            }
            current_token = scanner_next(); 
        }
        return ERR_OK;
    }

    // 3. Zpracování cyklu 
    else if (current_token.type == TOK_KW_WHILE) {
        current_token = scanner_next();
        
        if (current_token.type != TOK_LPAREN) error_exit(ERR_SYNTAX, "Očekávána '(' za 'while'");
        current_token = scanner_next();
        parse_expression();
        if (current_token.type != TOK_RPAREN) error_exit(ERR_SYNTAX, "Očekávána ')' za podmínkou");
        current_token = scanner_next();

        if (current_token.type != TOK_KW_DO) error_exit(ERR_SYNTAX, "Očekáváno 'do' po podmínce while");
        current_token = scanner_next();

        if (current_token.type != TOK_EOL) error_exit(ERR_SYNTAX, "Očekáván konec řádku za 'do'");
        current_token = scanner_next();

        if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'do'");
        current_token = scanner_next();

        // Tělo while bloku
        while (current_token.type != TOK_DEDENT) {
            if (current_token.type == TOK_EOL) { current_token = scanner_next(); continue; }
            parse_statement();
        }
        current_token = scanner_next(); 
        
        return ERR_OK;
    }

    // 4. Zpracování přiřazení do existující proměnné nebo volání funkce
    else if (current_token.type == TOK_IDENT) {
        current_token = scanner_next(); 

        // a) Přiřazení (např. x = 10)
        if (current_token.type == TOK_ASSIGN) {
            current_token = scanner_next(); 
            parse_expression();
            
            if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
                error_exit(ERR_SYNTAX, "Očekáván konec řádku za přiřazením");
            }
            if (current_token.type == TOK_EOL) current_token = scanner_next();
        }
        // b) Volání funkce (např. write(x, "test"))
        else if (current_token.type == TOK_LPAREN) {
            current_token = scanner_next(); 
            
            // Argumenty oddělené čárkou
            if (current_token.type != TOK_RPAREN) {
                while (true) {
                    parse_expression();
                    if (current_token.type == TOK_COMMA) {
                        current_token = scanner_next(); 
                    } else if (current_token.type == TOK_RPAREN) {
                        break; 
                    } else {
                        error_exit(ERR_SYNTAX, "Očekávána ',' nebo ')' ve volání funkce");
                    }
                }
            }
            current_token = scanner_next(); 

            if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
                error_exit(ERR_SYNTAX, "Očekáván konec řádku za voláním funkce");
            }
            if (current_token.type == TOK_EOL) current_token = scanner_next();
        } 
        else {
            error_exit(ERR_SYNTAX, "Očekáváno '=' pro přiřazení nebo '(' pro volání funkce");
        }
        
        return ERR_OK;
    }

    // 5. Návrat z funkce 
    else if (current_token.type == TOK_KW_RETURN) {
        current_token = scanner_next();
        parse_expression();
        
        if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
            error_exit(ERR_SYNTAX, "Očekáván konec řádku za 'return'");
        }
        if (current_token.type == TOK_EOL) current_token = scanner_next();
        
        return ERR_OK;
    }

    error_exit(ERR_SYNTAX, "Neočekávaný příkaz v bloku");
    return ERR_OK;
}

// Výrazy (precedenční analýza)
int parse_expression() {
    return ERR_OK;
}

// Rozšíření
int parse_list() {
    return ERR_OK;
}

int parse_control_flow() {
    return ERR_OK;
}

int parse_function_expression() {
    // 1. Def
    current_token = scanner_next();

    // 2. Název funkce
    if (current_token.type != TOK_IDENT) {
        error_exit(ERR_SYNTAX, "Očekáván název funkce po 'def'");
    }
    // TODO: Uložení názvu do tabulky symbolů
    current_token = scanner_next();

    // 3. Levá závorka
    if (current_token.type != TOK_LPAREN) {
        error_exit(ERR_SYNTAX, "Očekávána '(' za názvem funkce");
    }
    current_token = scanner_next();

    // 4. Parametry (kontrola 'identifikátor : Typ')
    if (current_token.type != TOK_RPAREN) {
        while (true) {
            // Název parametru
            if (current_token.type != TOK_IDENT) {
                error_exit(ERR_SYNTAX, "Očekáván název parametru");
            }
            current_token = scanner_next();

            // Dvojtečka
            if (current_token.type != TOK_COLON) {
                error_exit(ERR_SYNTAX, "Očekávána ':' za názvem parametru");
            }
            current_token = scanner_next();

            // Datový typ parametru
            if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
                current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
                current_token.type != TOK_KW_BOOLEAN) {
                error_exit(ERR_SYNTAX, "Očekáván datový typ parametru (Int, Double, String, Unit, Boolean)");
            }
            current_token = scanner_next();

            // Čárka pro další parametr, nebo konec závorky
            if (current_token.type == TOK_COMMA) {
                current_token = scanner_next(); 
            } else if (current_token.type == TOK_RPAREN) {
                break; 
            } else {
                error_exit(ERR_SYNTAX, "Očekávána ',' nebo ')' v parametrech funkce");
            }
        }
    }
    
    current_token = scanner_next();

    // 5. Návratový typ (očekáváme ':' a typ)
    if (current_token.type != TOK_COLON) {
        error_exit(ERR_SYNTAX, "Očekávána ':' a návratový typ funkce");
    }
    current_token = scanner_next();

    if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
        current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
        current_token.type != TOK_KW_BOOLEAN) {
        error_exit(ERR_SYNTAX, "Očekáván platný datový typ (Int, Double, String, Unit, Boolean)");
    }
    current_token = scanner_next();

    // 6. Znak přiřazení '='
    if (current_token.type != TOK_ASSIGN) {
        error_exit(ERR_SYNTAX, "Očekáváno '=' před tělem funkce");
    }
    current_token = scanner_next();

    // 7. Konec řádku před začátkem bloku
    if (current_token.type != TOK_EOL) {
        error_exit(ERR_SYNTAX, "Očekáván konec řádku za '='");
    }
    current_token = scanner_next();

    // 8. Začátek bloku (odsazení)
    if (current_token.type != TOK_INDENT) {
        error_exit(ERR_SYNTAX, "Očekáváno odsazení (INDENT) pro tělo funkce");
    }
    current_token = scanner_next();

    // 9. Čtení těla funkce 
    while (current_token.type != TOK_DEDENT) {
        if (current_token.type == TOK_EOF) {
            error_exit(ERR_SYNTAX, "Neočekávaný konec souboru uvnitř těla funkce");
        }
        
        // Přeskočíme prázdné řádky v těle funkce
        if (current_token.type == TOK_EOL) {
            current_token = scanner_next();
            continue;
        }

        // Zpracování konkrétního příkazu 
        parse_statement();
    }

    // 10. Konec bloku
    current_token = scanner_next();

    return ERR_OK;
}