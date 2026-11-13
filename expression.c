/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   expression.c - precedenční syntaktická analýza
 * Autor: xdundrm00
 */ 
#include <stdio.h> 
#include <stdlib.h> 
#include "expression.h"

// Indexy pro precedenční tabulku 
typedef enum { 
    E_PLUS_MINUS, // + - 
    E_UNARY_MINUS,  // - (unární)
    E_MUL_DIV,    // * / 
    E_REL,        // relační operátory (<, <=, >, >=, ==, !=) 
    E_OPERAND,    // operandy (identifikátory i, literály) 
    E_LPAREN,     // ( 
    E_RPAREN,     // ) 
    E_DOLLAR,     // $ (dno zásobníku / konec vstupu) 
    E_EXPR,       // Neterminál E 
    E_NONE 
} tPrecedenceSymbol;

// Položka na zásobníku
typedef struct tStackItem { 
    tPrecedenceSymbol symbol; 
    tToken token;
    tTypeInfo type;
    struct tStackItem* next; 
} tStackItem;

// Zásobník 
typedef struct { 
    tStackItem* top; 
} tStack;

// Precedenční tabulka  
static const char precedence_table[8][8] = { 
//                    + -    u-    * /   rel    i     (     )     $ 
/* + -       */     { '>',  '<',  '<',  '>',  '<',  '<',  '>',  '>' },
/* unární -  */     { '>',  '<',  '>',  '>',  '<',  '<',  '>',  '>' },
/* * /       */     { '>',  '<',  '>',  '>',  '<',  '<',  '>',  '>' },
/* rel       */     { '<',  '<',  '<',  '>',  '<',  '<',  '>',  '>' },
/* i         */     { '>',  ' ',  '>',  '>',  ' ',  ' ',  '>',  '>' },
/* (         */     { '<',  '<',  '<',  '<',  '<',  '<',  '=',  ' ' },
/* )         */     { '>',  '>',  '>',  '>',  ' ',  ' ',  '>',  '>' },
/* $         */     { '<',  '<',  '<',  '<',  '<',  '<',  ' ',  'A' } 
};

// Pomocné funkce pro zásobník 
static void stack_init(tStack* s) { 
    s->top = NULL; 
}

static int stack_push(tStack* s, tPrecedenceSymbol symbol, tToken token, tTypeInfo type) { 
    tStackItem* newItem = malloc(sizeof(tStackItem)); 
    if (newItem == NULL) { 
        return 0; 
    } 
    newItem->symbol = symbol; 
    newItem->token = token;
    newItem->type = type;
    newItem->next = s->top; 
    s->top = newItem; 
    return 1; 
}

static void stack_pop(tStack* s) { 
    if (s->top != NULL) { 
        tStackItem* temp = s->top; 
        s->top = s->top->next; 
        free(temp); 
    } 
}

static tPrecedenceSymbol stack_top_terminal(tStack* s) { 
    tStackItem* curr = s->top; 
    while (curr != NULL) { 
        if (curr->symbol != E_EXPR) { 
            return curr->symbol; 
        } 
        curr = curr->next; 
    } 
    return E_DOLLAR; 
}

static void stack_free(tStack* s) { 
    while (s->top != NULL) { 
        stack_pop(s); 
    } 
}

// Mapování tokenu 
static tPrecedenceSymbol token_to_precedence_symbol(tToken* token, tPrecedenceSymbol top_term) { 
    switch (token->type) { 
        case TOK_PLUS: 
        case TOK_MINUS: 
            if (top_term == E_PLUS_MINUS || top_term == E_UNARY_MINUS || 
                top_term == E_MUL_DIV || top_term == E_REL || 
                top_term == E_LPAREN || top_term == E_DOLLAR) {
                return E_UNARY_MINUS;
            }
            return E_PLUS_MINUS;
        case TOK_STAR:
        case TOK_SLASH:
            return E_MUL_DIV;
        case TOK_LT:
        case TOK_LE:
        case TOK_GT:
        case TOK_GE:
        case TOK_EQ:
        case TOK_NEQ:
            return E_REL;
        case TOK_IDENT:
        case TOK_INT_LIT:
        case TOK_DOUBLE_LIT:
        case TOK_STRING_LIT:
            return E_OPERAND;
        case TOK_LPAREN:
            return E_LPAREN;
        case TOK_RPAREN:
            return E_RPAREN;
        default:
            return E_NONE;
    }
}

// Precedenční analýza výrazu 
extern tToken current_token; 

int parse_expression_psa(tSemContext* ctx, tSymTable* local, tTypeInfo* out_type) { 
    tStack stack; 
    stack_init(&stack);

    tToken dummy_token = {0}; 
    tTypeInfo dummy_type = {TYPE_UNIT, TYPE_UNIT};

    // Vložení '$' na dno zásobníku
    if (!stack_push(&stack, E_DOLLAR, dummy_token, dummy_type)) {
        stack_free(&stack);
        return ERR_INTERNAL;
    }

    int result = ERR_OK;

    while (1) {
        tPrecedenceSymbol top_term = stack_top_terminal(&stack);
        tPrecedenceSymbol input_sym = token_to_precedence_symbol(&current_token, top_term);
        tPrecedenceSymbol table_input_sym = (input_sym == E_NONE) ? E_DOLLAR : input_sym;
        
        char relation = precedence_table[top_term][table_input_sym];

        // Shift nebo rovnost
        if (relation == '<' || relation == '=') {
            if (!stack_push(&stack, table_input_sym, current_token, dummy_type)) {
                result = ERR_INTERNAL;
                break;
            }
            if (input_sym != E_NONE) {
                get_next_token();
            }
        } 
        // Redukce
        else if (relation == '>') {
            // Pravidlo: E -> i
            if (stack.top->symbol == E_OPERAND && stack.top->next != NULL && stack.top->next->symbol != E_EXPR) {
                tToken operand_token = stack.top->token;
                stack_pop(&stack);

                tTypeInfo expr_type;
                // >> Ověření existence a zjištění typu operandu
                int sem_err = sem_resolve_term(ctx, local, operand_token, &expr_type);
                if (sem_err != ERR_OK) {
                    result = sem_err;
                    break;
                }

                if (!stack_push(&stack, E_EXPR, dummy_token, expr_type)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }
            // Pravidlo: E -> ( E )
            else if (stack.top->symbol == E_RPAREN && 
                     stack.top->next != NULL && stack.top->next->symbol == E_EXPR && 
                     stack.top->next->next != NULL && stack.top->next->next->symbol == E_LPAREN) {
                
                tTypeInfo inner_type = stack.top->next->type;
                
                stack_pop(&stack); // )
                stack_pop(&stack); // E
                stack_pop(&stack); // (
                
                if (!stack_push(&stack, E_EXPR, dummy_token, inner_type)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }
            // Pravidlo: E -> E op E
            else if (stack.top->symbol == E_EXPR && 
                     stack.top->next != NULL &&
                     (stack.top->next->symbol == E_PLUS_MINUS || 
                      stack.top->next->symbol == E_MUL_DIV || 
                      stack.top->next->symbol == E_REL) && 
                     stack.top->next->next != NULL && stack.top->next->next->symbol == E_EXPR) {
                
                tTypeInfo right_type = stack.top->type;
                tToken op_token = stack.top->next->token;
                tTypeInfo left_type = stack.top->next->next->type;
                
                stack_pop(&stack); // E
                stack_pop(&stack); // op
                stack_pop(&stack); // E
                
                tTypeInfo result_type;
                // >> Sémantická kontrola kompatibility typů pro binární operátor
                int sem_err = sem_check_binary_op(ctx, op_token.type, left_type, right_type, &result_type);
                if (sem_err != ERR_OK) {
                    result = sem_err;
                    break;
                }

                if (!stack_push(&stack, E_EXPR, dummy_token, result_type)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }

            // Pravidlo: E -> - E (unární minus)
            else if (stack.top->symbol == E_EXPR && 
                     stack.top->next != NULL && stack.top->next->symbol == E_UNARY_MINUS) {
                
                tTypeInfo operand_type = stack.top->type;
                
                stack_pop(&stack); // E
                stack_pop(&stack); // unární minus
                
                tTypeInfo result_type;
                int sem_err = sem_check_unary_op(TOK_MINUS, operand_type, &result_type);
                if (sem_err != ERR_OK) {
                    result = sem_err;
                    break;
                }

                if (!stack_push(&stack, E_EXPR, dummy_token, result_type)) {
                    result = ERR_INTERNAL;
                    break;
                }
            }

            else {
                result = ERR_SYNTAX;
                break;
            }
        } 
        // Akceptace
        else if (relation == 'A') {
            if (stack.top->symbol == E_EXPR && stack.top->next != NULL && stack.top->next->symbol == E_DOLLAR) {
                *out_type = stack.top->type;
                result = ERR_OK;
            } else {
                result = ERR_SYNTAX;
            }
            break;
        } 
        else {
            result = ERR_SYNTAX;
            break;
        }
    }

    stack_free(&stack);
    return result;
}