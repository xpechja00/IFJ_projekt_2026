#include "parser.h"

int parse_program();
int parse_statement();
int parse_expression();
int parse_list();
int parse_control_flow();
int parse_function_expression();

void advance_token() {
}

int parse() {
    advance_token();
    
    int result = parse_program();
    
    return result;
}

int parse_program() {
    return 0; 
}

int parse_statement() {
    return 0;
}

int parse_expression() {
    return 0;
}

int parse_list() {
    return 0;
}

int parse_control_flow() {
    return 0;
}

int parse_function_expression() {
    return 0;
}