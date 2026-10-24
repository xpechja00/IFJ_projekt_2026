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
#include <stdlib.h> 

// Aktuálně zpracovávaný token
static tToken current_token;

// Ukazatele na tabulky symbolů
static tSymTable* global_table = NULL;
static tSymTable* local_table = NULL;

// Převod tokenu na datový typ pro symtable
static tDataType token_to_type(tTokenType type) {
    switch(type) {
        case TOK_KW_INT: return TYPE_INT;
        case TOK_KW_DOUBLE: return TYPE_DOUBLE;
        case TOK_KW_STRING: return TYPE_STRING;
        case TOK_KW_BOOLEAN: return TYPE_BOOLEAN;
        default: return TYPE_UNIT;
    }
}

// Bezpečné načtení dalšího tokenu s uvolněním paměti toho předchozího
static void get_next_token() {
    if (current_token.type == TOK_IDENT || current_token.type == TOK_STRING_LIT) {
        if (current_token.value.str_val != NULL) {
            free(current_token.value.str_val);
        }
    }
    current_token = scanner_next();
}

// Zásobník pro precedenční analýzu
#define MAX_STACK 100

typedef struct {
    tTokenType items[MAX_STACK];
    int top;
} tStack;

static void stack_init(tStack* s) {
    s->top = -1;
}

static void stack_push(tStack* s, tTokenType type) {
    if (s->top >= MAX_STACK - 1) error_exit(ERR_INTERNAL, "Přetečení zásobníku výrazů");
    s->items[++(s->top)] = type;
}

static tTokenType stack_pop(tStack* s) {
    if (s->top < 0) error_exit(ERR_INTERNAL, "Zásobník výrazů je prázdný");
    return s->items[(s->top)--];
}

static tTokenType stack_top(tStack* s) {
    if (s->top < 0) return TOK_EOF; // Prázdný zásobník
    return s->items[s->top];
}

// TABULKA PRIORIT OPERÁTORŮ
// Vrací prioritu operátoru (vyšší číslo => vyšší priorita)
static int get_precedence(tTokenType type) {
    switch (type) {
        case TOK_STAR:
        case TOK_SLASH:
            return 40; // Násobení a dělení = nejvyšší priorita
        case TOK_PLUS:
        case TOK_MINUS:
        case TOK_CONCAT:
            return 30; // Sčítání, odčítání
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
        case TOK_EQ:
        case TOK_NEQ:
            return 20; // Relační operátory
        case TOK_AND:
            return 10; // Logické AND
        case TOK_OR:
            return 5;  // Logické OR
        default:
            return -1; // Není operátor
    }
}

// Deklarace interních funkcí pro rekurzivní sestup
int parse_program();
int parse_statement();

// Výrazy (precedenční analýza)
int parse_expression() {
    tStack op_stack;
    stack_init(&op_stack);
    bool expecting_operand = true;

    while (true) {
        if (expecting_operand) {
            // Zpracování levé závorky
            if (current_token.type == TOK_LPAREN) {
                stack_push(&op_stack, current_token.type);
                get_next_token();
            } else {
                // 1. Načtení základu operandu
                if (current_token.type == TOK_IDENT || current_token.type == TOK_KW_LIST) {
                    get_next_token();
                } else if (current_token.type == TOK_INT_LIT || current_token.type == TOK_DOUBLE_LIT || 
                           current_token.type == TOK_STRING_LIT || current_token.type == TOK_TRUE_LIT || 
                           current_token.type == TOK_FALSE_LIT) {
                    get_next_token();
                } else if (current_token.type == TOK_KW_IF) {
                    // IF jako výraz (rozšíření CONTROL)
                    get_next_token();
                    parse_expression(); // Vyhodnocení podmínky
                    if (current_token.type != TOK_KW_THEN) error_exit(ERR_SYNTAX, "Očekáváno 'then' ve výrazu if");
                    get_next_token();
                    parse_expression(); // Vyhodnocení 'then' větve
                    if (current_token.type != TOK_KW_ELSE) error_exit(ERR_SYNTAX, "Očekáváno 'else' ve výrazu if");
                    get_next_token();
                    parse_expression(); // Vyhodnocení 'else' větve
                } else {
                    error_exit(ERR_SYNTAX, "Očekáván operand nebo '('");
                }

                // 2. Zpracování sufixů (FUNEXP, VALMEM, LISTS)
                while (current_token.type == TOK_LPAREN || current_token.type == TOK_DOT) {
                    if (current_token.type == TOK_LPAREN) {
                        get_next_token(); 
                        if (current_token.type != TOK_RPAREN) {
                            while (true) {
                                parse_expression(); // Rekurzivní vyhodnocení argumentu
                                if (current_token.type == TOK_COMMA) {
                                    get_next_token();
                                } else if (current_token.type == TOK_RPAREN) {
                                    break;
                                } else {
                                    error_exit(ERR_SYNTAX, "Očekávána ',' nebo ')' v argumentech");
                                }
                            }
                        }
                        get_next_token(); 
                    } else if (current_token.type == TOK_DOT) {
                        get_next_token(); 
                        if (current_token.type != TOK_IDENT) error_exit(ERR_SYNTAX, "Očekáván identifikátor po '.'");
                        get_next_token(); 
                    }
                }
                
                expecting_operand = false; 
            }
        } else {
            // Zpracování pravé závorky nebo operátoru
            if (current_token.type == TOK_RPAREN) {
                bool found_lparen = false;
                
                // Vybírání ze zásobníku až po levou závorku
                while (stack_top(&op_stack) != TOK_EOF) {
                    tTokenType top_op = stack_pop(&op_stack);
                    if (top_op == TOK_LPAREN) {
                        found_lparen = true;
                        break;
                    }
                    // TODO: Předání operátoru do generátoru kódu
                }
                
                if (!found_lparen) {
                    break; 
                }
                get_next_token();
            } else if (get_precedence(current_token.type) > 0) {
                int current_prec = get_precedence(current_token.type);
                
                // Zpracování priorit operátorů
                while (stack_top(&op_stack) != TOK_EOF && stack_top(&op_stack) != TOK_LPAREN) {
                    if (get_precedence(stack_top(&op_stack)) >= current_prec) {
                        tTokenType top_op = stack_pop(&op_stack);
                        // TODO: Předání operátoru do generátoru kódu
                    } else {
                        break;
                    }
                }
                
                stack_push(&op_stack, current_token.type);
                get_next_token();
                expecting_operand = true;
            } else {
                break; 
            }
        }
    }

    if (expecting_operand) {
        error_exit(ERR_SYNTAX, "Neočekávaný konec výrazu");
    }

    // Vyprázdnění zbytku zásobníku
    while (stack_top(&op_stack) != TOK_EOF) {
        tTokenType top_op = stack_pop(&op_stack);
        if (top_op == TOK_LPAREN) {
            error_exit(ERR_SYNTAX, "Nespárovaná levá závorka ve výrazu");
        }
        // TODO: Předání operátoru do generátoru kódu
    }

    return ERR_OK;
}

int parse_list();
int parse_control_flow();
int parse_function_expression();

// Hlavní funkce
int parse() {
    global_table = symtable_create();
    if (!global_table) error_exit(ERR_INTERNAL, "Chyba alokace globalni tabulky");
    
    current_token = scanner_next(); 
    int result = parse_program();
    
    symtable_free(global_table);
    return result;
}

int parse_program() {
    // Prolog
    if (current_token.type != TOK_KW_IMPORT) 
        error_exit(ERR_SYNTAX, "Očekáváno klíčové slovo 'import'");
    get_next_token();
    
    if (current_token.type != TOK_IDENT || strcmp(current_token.value.str_val, "ifj26") != 0) 
        error_exit(ERR_SYNTAX, "Očekáván identifikátor 'ifj26'");
    get_next_token();
    
    if (current_token.type != TOK_DOT) 
        error_exit(ERR_SYNTAX, "Očekávána tečka za 'ifj26'");
    get_next_token();
    
    if (current_token.type != TOK_STAR) 
        error_exit(ERR_SYNTAX, "Očekávána hvězdička v prologu");
    get_next_token();
    
    // Hlavní cyklus zpracování na nejvyšší úrovni
    while (current_token.type != TOK_EOF) {
        if (current_token.type == TOK_EOL) {
            get_next_token();
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
        bool is_mutable = (current_token.type == TOK_KW_VAR); // True pro var, false pro val
        get_next_token();

        if (current_token.type != TOK_IDENT) error_exit(ERR_SYNTAX, "Očekáván název proměnné");
        
        // Kopírování názvu před posunem na další token
        char* var_name = malloc(strlen(current_token.value.str_val) + 1);
        if (!var_name) error_exit(ERR_INTERNAL, "Chyba alokace pameti");
        strcpy(var_name, current_token.value.str_val);
        get_next_token();

        tDataType var_type = TYPE_UNIT;
        if (current_token.type == TOK_COLON) {
            get_next_token();
            if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
                current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
                current_token.type != TOK_KW_BOOLEAN) {
                free(var_name);
                error_exit(ERR_SYNTAX, "Očekáván datový typ proměnné");
            }
            var_type = token_to_type(current_token.type); 
            get_next_token();
        } else {
            free(var_name);
            error_exit(ERR_SYNTAX, "Očekávána ':' a datový typ");
        }

        if (current_token.type != TOK_ASSIGN) {
            free(var_name);
            error_exit(ERR_SYNTAX, "Očekáváno '='");
        }
        get_next_token();

        parse_expression();

        if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
            free(var_name);
            error_exit(ERR_SYNTAX, "Očekáván konec řádku za definicí");
        }
        if (current_token.type == TOK_EOL) get_next_token();
        
        // Vložení do správné tabulky symbolů
        tData v_data;
        v_data.kind = SYM_VAR;
        v_data.attribute.var.typeInfo.type = var_type;
        v_data.attribute.var.typeInfo.innerType = TYPE_UNIT;
        v_data.attribute.var.isMutable = is_mutable;

        tSymTable* target_table = (local_table != NULL) ? local_table : global_table;
        
        if (symtable_insert(target_table, var_name, v_data) == INSERT_DUPLICATE) {
            free(var_name);
            error_exit(ERR_REDEF, "Redefinice proměnné"); 
        }
        free(var_name);
        
        return ERR_OK;
    }

    // 2. Zpracování podmínky (BOOLTHEN + CONTROL)
    else if (current_token.type == TOK_KW_IF) {
        bool has_paren;
        
        // IF větev
        get_next_token();
        has_paren = (current_token.type == TOK_LPAREN);
        if (has_paren) get_next_token();
        
        parse_expression();
        
        if (has_paren) {
            if (current_token.type != TOK_RPAREN) error_exit(ERR_SYNTAX, "Chybí pravá závorka v podmínce");
            get_next_token();
        }
        if (current_token.type != TOK_KW_THEN) error_exit(ERR_SYNTAX, "Očekáváno 'then'");
        get_next_token();

        // Tělo IF
        if (current_token.type == TOK_EOL) {
            get_next_token();
            if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'then'");
            get_next_token();
            while (current_token.type != TOK_DEDENT) {
                if (current_token.type == TOK_EOL) { get_next_token(); continue; }
                parse_statement();
            }
            get_next_token(); 
        } else {
            parse_statement(); // Jednořádkové tělo z rozšíření CONTROL
        }

        // Zpracování ELSE IF a ELSE (rozšíření BOOLTHEN)
        while (current_token.type == TOK_KW_ELSE) {
            get_next_token();
            
            if (current_token.type == TOK_KW_IF) {
                // ELSE IF
                get_next_token();
                has_paren = (current_token.type == TOK_LPAREN);
                if (has_paren) get_next_token();
                
                parse_expression();
                
                if (has_paren) {
                    if (current_token.type != TOK_RPAREN) error_exit(ERR_SYNTAX, "Chybí pravá závorka v podmínce");
                    get_next_token();
                }
                if (current_token.type != TOK_KW_THEN) error_exit(ERR_SYNTAX, "Očekáváno 'then'");
                get_next_token();

                if (current_token.type == TOK_EOL) {
                    get_next_token();
                    if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení");
                    get_next_token();
                    while (current_token.type != TOK_DEDENT) {
                        if (current_token.type == TOK_EOL) { get_next_token(); continue; }
                        parse_statement();
                    }
                    get_next_token(); 
                } else {
                    parse_statement();
                }
            } else {
                // ELSE
                if (current_token.type == TOK_EOL) {
                    get_next_token();
                    if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'else'");
                    get_next_token();
                    while (current_token.type != TOK_DEDENT) {
                        if (current_token.type == TOK_EOL) { get_next_token(); continue; }
                        parse_statement();
                    }
                    get_next_token(); 
                } else {
                    parse_statement();
                }
                break; 
            }
        }
        return ERR_OK;
    }

    // 3. Zpracování cyklu WHILE (S podporou CONTROL)
    else if (current_token.type == TOK_KW_WHILE) {
        get_next_token();
        
        bool has_paren = (current_token.type == TOK_LPAREN);
        if (has_paren) get_next_token();
        
        parse_expression();
        
        if (has_paren) {
            if (current_token.type != TOK_RPAREN) error_exit(ERR_SYNTAX, "Očekávána ')' za podmínkou");
            get_next_token();
        }

        if (current_token.type != TOK_KW_DO) error_exit(ERR_SYNTAX, "Očekáváno 'do'");
        get_next_token();

        if (current_token.type == TOK_EOL) {
            get_next_token();
            if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'do'");
            get_next_token();
            while (current_token.type != TOK_DEDENT) {
                if (current_token.type == TOK_EOL) { get_next_token(); continue; }
                parse_statement();
            }
            get_next_token(); 
        } else {
            parse_statement(); 
        }
        
        return ERR_OK;
    }

    // 4. Zpracování cyklu FOR (rozšíření CONTROL)
    else if (current_token.type == TOK_KW_FOR) {
        get_next_token();
        if (current_token.type != TOK_IDENT) error_exit(ERR_SYNTAX, "Očekávána řídicí proměnná pro for cyklus");
        
        get_next_token();
        
        if (current_token.type != TOK_ARROW_L) error_exit(ERR_SYNTAX, "Očekáváno '<-'");
        get_next_token();
        
        parse_expression(); 
        
        // 'to' a 'until' nejsou ve výčtu lexikálních tokenů jako KW
        if (current_token.type != TOK_IDENT || 
           (strcmp(current_token.value.str_val, "to") != 0 && strcmp(current_token.value.str_val, "until") != 0)) {
            error_exit(ERR_SYNTAX, "Očekáváno klíčové slovo 'to' nebo 'until'");
        }
        get_next_token();
        
        parse_expression();
        
        // Volitelný krok 'by'
        if (current_token.type == TOK_IDENT && strcmp(current_token.value.str_val, "by") == 0) {
            get_next_token();
            parse_expression(); 
        }
        
        if (current_token.type != TOK_KW_DO) error_exit(ERR_SYNTAX, "Očekáváno 'do'");
        get_next_token();
        
        if (current_token.type == TOK_EOL) {
            get_next_token();
            if (current_token.type != TOK_INDENT) error_exit(ERR_SYNTAX, "Očekáváno odsazení po 'do'");
            get_next_token();
            while (current_token.type != TOK_DEDENT) {
                if (current_token.type == TOK_EOL) { get_next_token(); continue; }
                parse_statement();
            }
            get_next_token(); 
        } else {
            parse_statement(); 
        }
        return ERR_OK;
    }

    // 5. Zpracování přiřazení do existující proměnné nebo volání funkce
    else if (current_token.type == TOK_IDENT) {
        char* name = malloc(strlen(current_token.value.str_val) + 1);
        if (!name) error_exit(ERR_INTERNAL, "Chyba alokace pameti");
        strcpy(name, current_token.value.str_val);
        get_next_token(); 

        // Vyhledání v lokální, pak v globální tabulce
        tData* symbol = NULL;
        if (local_table) symbol = symtable_search(local_table, name);
        if (!symbol && global_table) symbol = symtable_search(global_table, name);

        if (!symbol) {
            free(name);
            error_exit(ERR_UNDEF, "Pouziti nedefinovaneho identifikatoru");
        }

        // a) Přiřazení 
        if (current_token.type == TOK_ASSIGN) {
            if (symbol->kind == SYM_VAR && !symbol->attribute.var.isMutable) {
                free(name);
                error_exit(ERR_ASSIGN, "Prirazeni do konstanty (val)");
            }
            free(name);
            get_next_token(); 
            parse_expression();
            
            if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
                error_exit(ERR_SYNTAX, "Očekáván konec řádku za přiřazením");
            }
            if (current_token.type == TOK_EOL) get_next_token();
        }
        // b) Volání funkce 
        else if (current_token.type == TOK_LPAREN) {
            if (symbol->kind != SYM_FUNC) {
                free(name);
                error_exit(ERR_TYPE, "Pokus o volani promenne jako funkce");
            }
            free(name);
            get_next_token(); 
            
            // Argumenty oddělené čárkou
            if (current_token.type != TOK_RPAREN) {
                while (true) {
                    parse_expression();
                    if (current_token.type == TOK_COMMA) {
                        get_next_token(); 
                    } else if (current_token.type == TOK_RPAREN) {
                        break; 
                    } else {
                        error_exit(ERR_SYNTAX, "Očekávána ',' nebo ')' ve volání funkce");
                    }
                }
            }
            get_next_token(); 

            if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
                error_exit(ERR_SYNTAX, "Očekáván konec řádku za voláním funkce");
            }
            if (current_token.type == TOK_EOL) get_next_token();
        } 
        else {
            free(name);
            error_exit(ERR_SYNTAX, "Očekáváno '=' pro přiřazení nebo '(' pro volání funkce");
        }
        
        return ERR_OK;
    }

    // 6. Návrat z funkce 
    else if (current_token.type == TOK_KW_RETURN) {
        get_next_token();
        parse_expression();
        
        if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
            error_exit(ERR_SYNTAX, "Očekáván konec řádku za 'return'");
        }
        if (current_token.type == TOK_EOL) get_next_token();
        
        return ERR_OK;
    }

    error_exit(ERR_SYNTAX, "Neočekávaný příkaz v bloku");
    return ERR_OK;
}

int parse_function_expression() {
    int param_count = 0;
    // 1. Def
    get_next_token();

    // 2. Název funkce
    if (current_token.type != TOK_IDENT) {
        error_exit(ERR_SYNTAX, "Očekáván název funkce po 'def'");
    }
    
    // Kopírování názvu před posunem na další token
    char* func_name = malloc(strlen(current_token.value.str_val) + 1);
    if (!func_name) error_exit(ERR_INTERNAL, "Chyba alokace pameti");
    strcpy(func_name, current_token.value.str_val);
    get_next_token();

    // Založení nového lokálního rámce pro parametry a tělo funkce
    local_table = symtable_create();
    if (!local_table) error_exit(ERR_INTERNAL, "Chyba alokace lokalni tabulky");

    // 3. Levá závorka
    if (current_token.type != TOK_LPAREN) {
        error_exit(ERR_SYNTAX, "Očekávána '(' za názvem funkce");
    }
    get_next_token();

    // 4. Parametry 
    if (current_token.type != TOK_RPAREN) {
        while (true) {
            param_count++;
            // Název parametru
            if (current_token.type != TOK_IDENT) {
                error_exit(ERR_SYNTAX, "Očekáván název parametru");
            }
            get_next_token();

            // Dvojtečka
            if (current_token.type != TOK_COLON) {
                error_exit(ERR_SYNTAX, "Očekávána ':' za názvem parametru");
            }
            get_next_token();

            // Datový typ parametru
            if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
                current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
                current_token.type != TOK_KW_BOOLEAN) {
                error_exit(ERR_SYNTAX, "Očekáván datový typ parametru (Int, Double, String, Unit, Boolean)");
            }
            get_next_token();

            // Čárka pro další parametr, nebo konec závorky
            if (current_token.type == TOK_COMMA) {
                get_next_token(); 
            } else if (current_token.type == TOK_RPAREN) {
                break; 
            } else {
                error_exit(ERR_SYNTAX, "Očekávána ',' nebo ')' v parametrech funkce");
            }
        }
    }
    
    get_next_token();

    // 5. Návratový typ 
    if (current_token.type != TOK_COLON) {
        error_exit(ERR_SYNTAX, "Očekávána ':' a návratový typ funkce");
    }
    get_next_token();

    if (current_token.type != TOK_KW_INT && current_token.type != TOK_KW_DOUBLE && 
        current_token.type != TOK_KW_STRING && current_token.type != TOK_KW_UNIT &&
        current_token.type != TOK_KW_BOOLEAN) {
        error_exit(ERR_SYNTAX, "Očekáván platný datový typ (Int, Double, String, Unit, Boolean)");
    }
    get_next_token();

    // 6. Znak přiřazení 
    if (current_token.type != TOK_ASSIGN) {
        error_exit(ERR_SYNTAX, "Očekáváno '=' před tělem funkce");
    }
    get_next_token();

    // 7. Rozhodnutí o typu těla (blok s odsazením / jeden výraz pro FUNEXP)
    if (current_token.type == TOK_EOL) {
        get_next_token();
        
        // 8. Začátek bloku 
        if (current_token.type != TOK_INDENT) {
            error_exit(ERR_SYNTAX, "Očekáváno odsazení (INDENT) pro tělo funkce");
        }
        get_next_token();

        // 9. Čtení těla funkce 
        while (current_token.type != TOK_DEDENT) {
            if (current_token.type == TOK_EOF) error_exit(ERR_SYNTAX, "Neočekávaný konec souboru");
            if (current_token.type == TOK_EOL) {
                get_next_token();
                continue;
            }
            parse_statement();
        }
        // 10. Konec bloku
        get_next_token();
        
    } else {
        // Zkrácený zápis funkce 
        parse_expression();
        if (current_token.type != TOK_EOL && current_token.type != TOK_EOF) {
            error_exit(ERR_SYNTAX, "Očekáván konec řádku za tělem zkrácené funkce");
        }
        if (current_token.type == TOK_EOL) get_next_token();
    }

    // Vložení samotné funkce do globální tabulky
    tData f_data;
    f_data.kind = SYM_FUNC;
    f_data.attribute.func.paramCount = param_count;
    f_data.attribute.func.params = NULL;
    f_data.attribute.func.isDefined = true;
    
    // Návratový typ bychom správně měli vyčíst, pro teď tam dáme UNIT
    f_data.attribute.func.returnType.type = TYPE_UNIT;
    f_data.attribute.func.returnType.innerType = TYPE_UNIT; 
    
    if (symtable_insert(global_table, func_name, f_data) == INSERT_DUPLICATE) {
        error_exit(ERR_REDEF, "Redefinice funkce"); 
    }
    free(func_name);

    // Zrušení lokální tabulky při opuštění funkce
    symtable_free(local_table);
    local_table = NULL;

    return ERR_OK;
}