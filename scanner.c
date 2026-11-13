/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   scanner.c - lexikální analyzátor (konečný automat)
 *
 * Autor: xpechja00
 *
 */

#include "scanner.h"
#include "error.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pomocná makra
#define is_digit(c) ((c) >= '0' && (c) <= '9')
#define is_letter(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define is_ident_char(c) (is_letter(c) || is_digit(c) || (c) == '_')

#define INDENT_MAX 256 // max. hloubka zanoření bloků
#define QUEUE_MAX (INDENT_MAX + 4) // EOL + všechny DEDENTy + rezerva

// Stavy automatu

typedef enum {
    S_START,

    // identifikátor
    S_ID_UNDERSCORE, // přečteno '_'
    S_ID, // uvnitř identifikátoru

    // čísla
    S_NUM_INT,
    S_NUM_ZERO,
    S_NUM_DOT, // tečka na začátku lexému
    S_NUM_INT_DOT,
    S_NUM_FRAC,
    S_NUM_EXP_START,
    S_NUM_EXP_SIGN,
    S_NUM_EXP,
    S_NUM_SUFFIX,
    S_NUM_INT_US,
    S_NUM_FRAC_US,
    S_NUM_EXP_US,

    // operátory
    S_LT,
    S_GT,
    S_ASSIGN,
    S_EXCL,
    S_PLUS,
    S_AMP,
    S_PIPE,

    // anotace
    S_AT, // přečteno '@', čte se "main"

    // lomítko a komentáře
    S_SLASH,
    S_LINE_COMMENT,
    S_BLOCK_COMMENT,
    S_BLOCK_STAR, // uvnitř komentáře, viděl '*'
    S_BLOCK_SLASH, // uvnitř komentáře, viděl '/'

    // řetězce
    S_STRING_OPEN, // přečteno "
    S_STRING, // uvnitř "..."
    S_STRING_ESC, // uvnitř, po '\'
    S_STRING_EMPTY, // přečteno ""
    S_ML_STRING, // uvnitř """..."""
    S_ML_QUOTES, // uvnitř """, čtou se uvozovky (možný konec)

} tState;

static const struct {
    const char* text;
    tTokenType type;
} keywords[] = {
    { "def", TOK_KW_DEF },
    { "do", TOK_KW_DO },
    { "else", TOK_KW_ELSE },
    { "if", TOK_KW_IF },
    { "import", TOK_KW_IMPORT },
    { "return", TOK_KW_RETURN },
    { "then", TOK_KW_THEN },
    { "val", TOK_KW_VAL },
    { "var", TOK_KW_VAR },
    { "while", TOK_KW_WHILE },
    { "for", TOK_KW_FOR },
    { "case", TOK_KW_CASE },
    { "class", TOK_KW_CLASS },
    { "extension", TOK_KW_EXTENSION },
    { "match", TOK_KW_MATCH },
    { "type", TOK_KW_TYPE },
    { "Int", TOK_KW_INT },
    { "Double", TOK_KW_DOUBLE },
    { "String", TOK_KW_STRING },
    { "Unit", TOK_KW_UNIT },
    { "Boolean", TOK_KW_BOOLEAN },
    { "List", TOK_KW_LIST },
    { "true", TOK_TRUE_LIT },
    { "false", TOK_FALSE_LIT },
};

static int line = 1;
static int pushback;
static bool has_pushback = false;

static void lex_error(int ln, const char* msg)
{
    error_exit(ERR_LEX, "řádek %d: %s", ln, msg);
}

static int next_char(void)
{
    int c;
    if (has_pushback) {
        c = pushback;
        has_pushback = false;
    } else {
        c = getchar();
    }
    if (c == '\n')
        line++;
    if (c != EOF && c > 127)
        lex_error(line, "znak mimo ASCII");
    return c;
}

// Vrátí jeden znak na vstup
static void unget_char(int c)
{
    if (c == '\n')
        line--;
    pushback = c;
    has_pushback = true;
}

// Buffer lexemu

static char* buf = NULL;
static size_t buf_len = 0;
static size_t buf_cap = 0;

static void buf_reset(void)
{
    buf_len = 0;
    if (buf != NULL)
        buf[0] = '\0';
}

static void buf_add(char c)
{
    if (buf_len + 1 >= buf_cap) {
        size_t new_cap = (buf_cap == 0) ? 32 : buf_cap * 2;
        char* tmp = realloc(buf, new_cap);
        if (tmp == NULL)
            error_exit(ERR_INTERNAL, "nedostatek paměti");
        buf = tmp;
        buf_cap = new_cap;
    }
    buf[buf_len++] = c;
    buf[buf_len] = '\0';
}

// Vrátí novou kopii bufferu (strdup není v C11).
static char* buf_copy(void)
{
    char* s = malloc(buf_len + 1);
    if (s == NULL)
        error_exit(ERR_INTERNAL, "nedostatek paměti");
    memcpy(s, buf != NULL ? buf : "", buf_len + 1);
    return s;
}

// Fronta čekajících tokenů

static tToken queue[QUEUE_MAX];
static int q_head = 0;
static int q_count = 0;

static void queue_push(tToken t)
{
    if (q_count == QUEUE_MAX)
        error_exit(ERR_INTERNAL, "přetečení fronty tokenů");
    queue[(q_head + q_count) % QUEUE_MAX] = t;
    q_count++;
}

static tToken queue_pop(void)
{
    tToken t = queue[q_head];
    q_head = (q_head + 1) % QUEUE_MAX;
    q_count--;
    return t;
}

// Pomocné proměnné

static int indent_stack[INDENT_MAX] = { 0 }; // na dně 0
static int indent_top = 0;
static int paren_depth = 0; // ( ) [ ]
static bool line_has_token = false; // na aktuálním logickém řádku už byl token
static bool nl_pending = false; // od posledního tokenu byl '\n' (-> nl_before)
static bool at_line_start = true;
static int pending_indent = 0;

static int comment_depth = 0; // hloubka vnoření /* */
static bool comment_had_nl = false; // v blokovém komentáři byl '\n'
static bool expect_eol = false; // po víceřádkovém komentáři smí být jen bílé znaky

// Vytvoří token bez hodnoty a vyplní společné položky.
static tToken make_token(tTokenType type, int tok_line)
{
    tToken t;
    memset(&t, 0, sizeof(t));
    t.type = type;
    t.line = tok_line;
    if (type == TOK_EOL || type == TOK_INDENT || type == TOK_DEDENT) {
        t.nl_before = true;
        return t;
    }
    t.nl_before = nl_pending;
    nl_pending = false;
    line_has_token = true;
    return t;
}

// Porovná pending_indent se zásobníkem, INDENT/DEDENTy dá do fronty.
// Vrací true, pokud něco přidal.
static bool handle_indent(void)
{
    if (pending_indent > indent_stack[indent_top]) {
        if (indent_top + 1 >= INDENT_MAX)
            error_exit(ERR_INTERNAL, "příliš hluboké zanoření");
        indent_stack[++indent_top] = pending_indent;
        queue_push(make_token(TOK_INDENT, line));
        return true;
    }
    bool added = false;
    while (pending_indent < indent_stack[indent_top]) {
        indent_top--;
        queue_push(make_token(TOK_DEDENT, line));
        added = true;
    }
    if (pending_indent != indent_stack[indent_top])
        error_exit(ERR_SYNTAX, "řádek %d: odsazení neodpovídá žádné předchozí úrovni", line);
    return added;
}

// Úklid a inicializace

static void scanner_cleanup(void)
{
    free(buf);
    buf = NULL;
    buf_len = buf_cap = 0;
    while (q_count > 0) {
        tToken t = queue_pop();
        token_free(&t);
    }
}

static void scanner_init(void)
{
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        atexit(scanner_cleanup); // úklid i při error_exit
    }
}

void token_free(tToken* token)
{
    if (token == NULL)
        return;
    if (token->type == TOK_IDENT || token->type == TOK_STRING_LIT) {
        free(token->value.str_val);
        token->value.str_val = NULL;
    }
}

// Hlavní funkce

tToken scanner_next(void)
{
    scanner_init();

    // nejdřív vydat čekající tokeny
    if (q_count > 0)
        return queue_pop();

    bool saw_tab = false;
    if (at_line_start) {
        at_line_start = false;
        pending_indent = 0;
        int c;
        while ((c = next_char()) == ' ' || c == '\t') {
            if (c == '\t')
                saw_tab = true;
            pending_indent++;
        }
        if(saw_tab){
            if(c != EOF && c != '\n'){
                lex_error(line, "tab na neprázdném řádku");
            }
        }
        unget_char(c); // první znak, který není mezera, patří dalšímu kroku
        if (paren_depth > 0 && c != '\n' && c != EOF && pending_indent <= indent_stack[indent_top])
            error_exit(ERR_SYNTAX, "řádek %d: řádek v závorkách musí být více odsazen", line);
    }

    buf_reset();
    tState state = S_START;
    int tok_line = line;
    int quote_count = 0;
    int c;

    while (true) {
        c = next_char();

        switch (state) {

        case S_START:
            if (c == ' ' || c == '\t' || c == '\r')
                break; // bílý znak mezi tokeny
            if (c == '\n') {
                expect_eol = false;
                at_line_start = true;
                nl_pending = true;
                if (paren_depth == 0 && line_has_token) {
                    line_has_token = false;
                    return make_token(TOK_EOL, line - 1);
                }
                return scanner_next(); // závorky nebo prázdný řádek
            }
            if (c == EOF) {
                if (line_has_token) { // soubor nekončí '\n' -> nejdřív EOL
                    line_has_token = false;
                    unget_char(EOF);
                    return make_token(TOK_EOL, line);
                }
                pending_indent = 0; // zavřít všechny bloky
                if (handle_indent()) { // DEDENTy do fronty
                    unget_char(EOF);
                    return queue_pop();
                }
                return make_token(TOK_EOF, line);
            }

            if (expect_eol && c != '/')
                error_exit(ERR_SYNTAX, "řádek %d: za víceřádkovým komentářem musí být konec řádku", line);

            if (!line_has_token && c != '/' && handle_indent()) {
                unget_char(c);
                return queue_pop();
            }
            tok_line = line;

            if (is_letter(c)) {
                buf_add((char)c);
                state = S_ID;
            } else if (c == '_') {
                buf_add((char)c);
                state = S_ID_UNDERSCORE;
            } else if (c == '<') {
                state = S_LT;
            } else if (c == '.') {
                buf_add('.');
                state = S_NUM_DOT;
            } else if (c == '>') {
                state = S_GT;
            } else if (c == '=') {
                state = S_ASSIGN;
            } else if (c == '!') {
                state = S_EXCL;
            } else if (c == '+') {
                state = S_PLUS;
            } else if (c == '&') {
                state = S_AMP;
            } else if (c == '|') {
                state = S_PIPE;
            } else if (c == '@') {
                state = S_AT;
            } else if (c == '/') {
                state = S_SLASH;
            } else if (c == '"') {
                state = S_STRING_OPEN;
            } else if (c == '0') {
                buf_add((char)c);
                state = S_NUM_ZERO;
            } else if (is_digit(c)) {
                buf_add((char)c);
                state = S_NUM_INT;
            } else if (c == '*') {
                return make_token(TOK_STAR, tok_line);
            } else if (c == '(') {
                paren_depth++;
                return make_token(TOK_LPAREN, tok_line);
            } else if (c == ')') {
                if (paren_depth > 0)
                    paren_depth--;
                return make_token(TOK_RPAREN, tok_line);
            } else if (c == ',') {
                return make_token(TOK_COMMA, tok_line);
            } else if (c == '[') {
                paren_depth++;
                return make_token(TOK_LBRACKET, tok_line);
            } else if (c == ']') {
                if (paren_depth > 0)
                    paren_depth--;
                return make_token(TOK_RBRACKET, tok_line);
            } else if (c == ':') {
                return make_token(TOK_COLON, tok_line);
            } else if (c == ';') {
                return make_token(TOK_SEMICOLON, tok_line);
            } else if (c == '-') {
                return make_token(TOK_MINUS, tok_line);
            } else {
                lex_error(tok_line, "neznámý znak");
            }
            break;

            // Operátory

        case S_LT:
            if (c == '=')
                return make_token(TOK_LE, tok_line);
            if (c == '-')
                return make_token(TOK_ARROW_L, tok_line);
            unget_char(c);
            return make_token(TOK_LT, tok_line);

        case S_GT:
            if (c == '=') {
                return make_token(TOK_GE, tok_line);
            }
            unget_char(c);
            return make_token(TOK_GT, tok_line);

        case S_ASSIGN:
            if (c == '=') {
                return make_token(TOK_EQ, tok_line);
            }
            unget_char(c);
            return make_token(TOK_ASSIGN, tok_line);

        case S_EXCL:
            if (c == '=') {
                return make_token(TOK_NEQ, tok_line);
            }
            unget_char(c);
            return make_token(TOK_NOT, tok_line);

        case S_PLUS:
            if (c == '+') {
                return make_token(TOK_CONCAT, tok_line);
            }
            unget_char(c);
            return make_token(TOK_PLUS, tok_line);

        case S_AMP:
            if (c == '&') {
                return make_token(TOK_AND, tok_line);
            }
            unget_char(c);
            lex_error(tok_line, "neznámý operátor");
            break;

        case S_PIPE:
            if (c == '|') {
                return make_token(TOK_OR, tok_line);
            }
            unget_char(c);
            lex_error(tok_line, "neznámý operátor");
            break;

        case S_AT:
            if (is_ident_char(c)) {
                buf_add((char)c);
                break;
            }
            unget_char(c);
            if (buf_len == 4 && strcmp(buf, "main") == 0)
                return make_token(TOK_ANNOT_MAIN, tok_line);
            lex_error(tok_line, "po @ musí následovat main");
            break;

            // ID

        case S_ID_UNDERSCORE:
            if (is_ident_char(c)) {
                buf_add((char)c);
                state = S_ID;
                break;
            }
            lex_error(tok_line, "samotné '_' není identifikátor");
            break;

        case S_ID:
            if (is_ident_char(c)) {
                buf_add(c);
            } else {
                unget_char(c);
                for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
                    if (strcmp(buf, keywords[i].text) == 0) {
                        return make_token(keywords[i].type, tok_line);
                    }
                }
                tToken t = make_token(TOK_IDENT, tok_line);
                t.value.str_val = buf_copy();
                return t;
            }
            break;

            // lomeno a komentáře

        case S_SLASH:
            if (c == '/') {
                state = S_LINE_COMMENT;
            } else if (c == '*') {
                comment_depth = 1;
                comment_had_nl = false;
                state = S_BLOCK_COMMENT;
            } else {
                unget_char(c);
                return make_token(TOK_SLASH, tok_line);
            }
            break;

        case S_LINE_COMMENT:
            if (c == '\n' || c == EOF) {
                unget_char(c);
                state = S_START;
            }
            break;

        case S_BLOCK_COMMENT:
            if (c == '*') {
                state = S_BLOCK_STAR;
            } else if (c == '/') {
                state = S_BLOCK_SLASH;
            } else if (c == '\n') {
                comment_had_nl = true;
            } else if (c == EOF) {
                lex_error(tok_line, "neukončený komentář");
            }
            break;

        case S_BLOCK_STAR:
            if (c == '/') {
                comment_depth--;
                if (comment_depth == 0) {
                    state = S_START;
                    if (comment_had_nl) {
                        nl_pending = true;
                        expect_eol = true;
                        if (paren_depth == 0 && line_has_token) {
                            line_has_token = false;
                            return make_token(TOK_EOL, line);
                        }
                    }
                    break;
                }
                state = S_BLOCK_COMMENT;
            } else if (c == '*') {
                break;
            } else if (c == EOF) {
                lex_error(tok_line, "neukončený komentář");
            } else if (c == '\n') {
                comment_had_nl = true;
            }
            state = S_BLOCK_COMMENT;
            break;

        case S_BLOCK_SLASH:
            state = S_BLOCK_COMMENT;
            if (c == '*') {
                comment_depth++;
                break;
            } else if (c == '/') {
                state = S_BLOCK_SLASH;
                break;
            } else if (c == '\n') {
                comment_had_nl = true;
                break;
            } else if (c == EOF) {
                lex_error(tok_line, "neukončený komentář");
            }
            break;

            // int a double

        case S_NUM_INT:
            if (c == '_') {
                state = S_NUM_INT_US;
                break;
            } else if (c == '.') {
                state = S_NUM_INT_DOT;
            } else if (c == 'e' || c == 'E') {
                state = S_NUM_EXP_START;
            } else if (is_digit(c)) {
                // zustat v S_NUM+INT
            } else {
                unget_char(c);
                tToken token = make_token(TOK_INT_LIT, tok_line);
                token.value.int_val = (int32_t)strtol(buf, NULL, 10);
                return token;
            }
            buf_add(c);
            break;

        case S_NUM_DOT:
            if (is_digit(c)) {
                state = S_NUM_FRAC;
            } else {
                unget_char(c);
                return make_token(TOK_DOT, tok_line);
            }
            buf_add(c);
            break;

        case S_NUM_ZERO:
            if (c == '.') {
                state = S_NUM_INT_DOT;
            } else if (c == 'e' || c == 'E') {
                state = S_NUM_EXP_START;
            } else {
                unget_char(c);
                tToken token = make_token(TOK_INT_LIT, tok_line);
                token.value.int_val = (int32_t)strtol(buf, NULL, 10);
                return token;
            }
            buf_add(c);
            break;

        case S_NUM_INT_DOT:
            if (is_digit(c)) {
                state = S_NUM_FRAC;
            } else {
                unget_char(c);
                tToken token = make_token(TOK_INT_LIT, tok_line);
                token.value.int_val = (int32_t)strtol(buf, NULL, 10);
                queue_push(make_token(TOK_DOT, tok_line));
                return token;
            }
            buf_add(c);
            break;

        case S_NUM_EXP_START:
            if (is_digit(c)) {
                state = S_NUM_EXP;
            } else if (c == '+' || c == '-') {
                state = S_NUM_EXP_SIGN;
            } else {
                lex_error(tok_line, "za 'e' musí následovat znaménko nebo číslice");
            }
            buf_add(c);
            break;

        case S_NUM_FRAC:
            if (is_digit(c)) {
                state = S_NUM_FRAC;
            } else if (c == '_') {
                state = S_NUM_FRAC_US;
                break;
            } else if (c == 'e' || c == 'E') {
                state = S_NUM_EXP_START;
            } else if (c == 'd' || c == 'D') {
                state = S_NUM_SUFFIX;
                break;
            } else {
                unget_char(c);
                tToken token = make_token(TOK_DOUBLE_LIT, tok_line);
                token.value.double_val = strtod(buf, NULL);
                return token;
            }
            buf_add(c);
            break;

        case S_NUM_EXP:
            if (is_digit(c)) {
                state = S_NUM_EXP;
            } else if (c == '_') {
                state = S_NUM_EXP_US;
                break;
            } else if (c == 'd' || c == 'D') {
                state = S_NUM_SUFFIX;
                break;
            } else {
                unget_char(c);
                tToken token = make_token(TOK_DOUBLE_LIT, tok_line);
                token.value.double_val = strtod(buf, NULL);
                return token;
            }
            buf_add(c);
            break;

        case S_NUM_SUFFIX:
            unget_char(c);
            tToken token = make_token(TOK_DOUBLE_LIT, tok_line);
            token.value.double_val = strtod(buf, NULL);
            return token;

        case S_NUM_EXP_SIGN:
            if (is_digit(c)) {
                state = S_NUM_EXP;
                buf_add(c);
                break;
            }
            lex_error(tok_line, "za znaménkem exponentu musí následovat číslice");
            break;
        case S_NUM_INT_US:
            if (is_digit(c)) {
                state = S_NUM_INT;
                buf_add(c);
                break;
            } else if (c == '_') {
                break;
            }
            lex_error(tok_line, "podtržítko musí být mezi číslicemi");
            break;

        case S_NUM_FRAC_US:
            if (is_digit(c)) {
                state = S_NUM_FRAC;
                buf_add(c);
                break;
            } else if (c == '_') {
                break;
            }
            lex_error(tok_line, "podtržítko musí být mezi číslicemi");
            break;

        case S_NUM_EXP_US:
            if (is_digit(c)) {
                state = S_NUM_EXP;
                buf_add(c);
                break;
            } else if (c == '_') {
                break;
            }
            lex_error(tok_line, "podtržítko musí být mezi číslicemi");
            break;

            // string

        case S_STRING_OPEN:
            if (c == '"') {
                state = S_STRING_EMPTY;
            } else {
                state = S_STRING;
                unget_char(c);
            }
            break;

        case S_STRING_EMPTY: 
            if (c == '"') {
                state = S_ML_STRING;
            } else {
                unget_char(c);
                tToken token = make_token(TOK_STRING_LIT, tok_line);
                token.value.str_val = buf_copy();
                return token;
            }
            break;

        case S_STRING:
            if (c == '"') {
                tToken token = make_token(TOK_STRING_LIT, tok_line);
                token.value.str_val = buf_copy();
                return token;
            } else if (c == '\\') {
                state = S_STRING_ESC;
                break;
            } else if (c == '\n' || c == EOF) {
                lex_error(tok_line, "neukončený řetězec");
                break;
            }
            buf_add(c);
            break;

        case S_STRING_ESC:
            if (c == '\\') {
                buf_add('\\');
            } else if (c == '"') {
                buf_add('"');
            } else if (c == 'n') {
                buf_add('\n');
            } else if (c == 'r') {
                buf_add('\r');
            } else if (c == 't') {
                buf_add('\t');
            } else {
                lex_error(tok_line, "neplatná escape sekvence");
            }
            state = S_STRING;
            break;

        case S_ML_STRING:
            if (c == '"') {
                quote_count = 1;
                state = S_ML_QUOTES;

            } else if (c == EOF) {
                lex_error(tok_line, "neukončený řetězec");
            } else {
                buf_add(c);
            }
            break;

        case S_ML_QUOTES:
            if (c == '"') {
                quote_count++;
                break;
            }
            if (quote_count >= 3) { // konec řetězce
                for (int i = 0; i < quote_count - 3; i++)
                    buf_add('"'); // přebytečné uvozovky patří do obsahu
                unget_char(c);
                tToken token = make_token(TOK_STRING_LIT, tok_line);
                token.value.str_val = buf_copy();
                return token;
            }
            for (int i = 0; i < quote_count; i++) // méně než 3 -> byly to obyčejné znaky
                buf_add('"');
            if (c == EOF)
                lex_error(tok_line, "neukončený víceřádkový řetězec");
            buf_add((char)c);
            state = S_ML_STRING;
            break;
        default:
            error_exit(ERR_INTERNAL, "neošetřený stav automatu %d", (int)state);
        }
    }
}
