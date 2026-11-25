/*
 * Projekt: Implementace překladače imperativního jazyka IFJ26
 * Modul:   scanner_test.c - jednotkové testy lexikálního analyzátoru
 *
 * Autor: xpechja00
 *
 * Testy napsány s pomocí AI (Claude). Prompt: "napis mi unit test na tenhle
 * scanner". Nejsou součástí odevzdávaného překladače.
 *
 * Každý test běží v samostatném procesu (fork): scanner má globální stav,
 * čte ze stdin a při chybě volá exit(). Rodič pošle vstup rourou na stdin
 * potomka, přečte vypsané tokeny a porovná je s očekáváním a návratovým kódem.
 *
 * Překlad a spuštění:
 *   gcc -std=c11 -Wall -Wextra -g scanner_test.c ../scanner.c ../error.c -I.. -o scanner_test
 *   ./scanner_test                 # všechny testy
 *   ./scanner_test -v              # vypíše i výstup úspěšných testů
 *   valgrind -q --trace-children=yes --leak-check=full ./scanner_test
 */

#define _POSIX_C_SOURCE 200809L

#include "scanner.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Výpis tokenů
// ---------------------------------------------------------------------------

static const char *tok_name[] = {
    [TOK_EOF] = "EOF",          [TOK_EOL] = "EOL",
    [TOK_INDENT] = "INDENT",    [TOK_DEDENT] = "DEDENT",
    [TOK_IDENT] = "ID",         [TOK_INT_LIT] = "INT",
    [TOK_UNDERSCORE] = "_",
    [TOK_DOUBLE_LIT] = "DBL",   [TOK_STRING_LIT] = "STR",
    [TOK_TRUE_LIT] = "true",    [TOK_FALSE_LIT] = "false",
    [TOK_KW_DEF] = "def",       [TOK_KW_DO] = "do",
    [TOK_KW_ELSE] = "else",     [TOK_KW_IF] = "if",
    [TOK_KW_IMPORT] = "import", [TOK_KW_RETURN] = "return",
    [TOK_KW_THEN] = "then",     [TOK_KW_VAL] = "val",
    [TOK_KW_VAR] = "var",       [TOK_KW_WHILE] = "while",
    [TOK_KW_FOR] = "for",       [TOK_KW_CASE] = "case",
    [TOK_KW_CLASS] = "class",   [TOK_KW_EXTENSION] = "extension",
    [TOK_KW_MATCH] = "match",   [TOK_KW_TYPE] = "type",
    [TOK_KW_INT] = "Int",       [TOK_KW_DOUBLE] = "Double",
    [TOK_KW_STRING] = "String", [TOK_KW_UNIT] = "Unit",
    [TOK_KW_BOOLEAN] = "Boolean", [TOK_KW_LIST] = "List",
    [TOK_PLUS] = "+",   [TOK_MINUS] = "-",  [TOK_STAR] = "*",  [TOK_SLASH] = "/",
    [TOK_LT] = "<",     [TOK_LE] = "<=",    [TOK_GT] = ">",    [TOK_GE] = ">=",
    [TOK_EQ] = "==",    [TOK_NEQ] = "!=",   [TOK_ASSIGN] = "=",
    [TOK_NOT] = "!",    [TOK_AND] = "&&",   [TOK_OR] = "||",
    [TOK_CONCAT] = "++", [TOK_ARROW_L] = "<-",
    [TOK_LPAREN] = "(", [TOK_RPAREN] = ")", [TOK_LBRACKET] = "[", [TOK_RBRACKET] = "]",
    [TOK_COMMA] = ",",  [TOK_COLON] = ":",  [TOK_SEMICOLON] = ";", [TOK_DOT] = ".",
    [TOK_ANNOT_MAIN] = "@main",
};
#define TOK_NAME_COUNT (sizeof(tok_name) / sizeof(tok_name[0]))

// Co se má u tokenu vypisovat navíc (aby očekávané výstupy byly čitelné,
// zapínají se jen v testech, které to kontrolují).
enum {
    SHOW_NL   = 1,   // '^' za tokenem s nl_before == true
    SHOW_LINE = 2,   // ':<řádek>' za tokenem
};

static void print_string_value(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        switch (*s) {
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '"':  fputs("\\\"", stdout); break;
        default:   putchar(*s);
        }
    }
    putchar('"');
}

// Tělo potomka: čte tokeny ze stdin a vypisuje je na jeden řádek.
static void dump_tokens(int flags)
{
    tToken t;
    bool first = true;
    do {
        t = scanner_next();
        if (!first)
            putchar(' ');
        first = false;

        if ((size_t)t.type < TOK_NAME_COUNT && tok_name[t.type] != NULL)
            fputs(tok_name[t.type], stdout);
        else
            printf("?%d", (int)t.type);

        switch (t.type) {
        case TOK_IDENT:      printf("(%s)", t.value.str_val); break;
        case TOK_INT_LIT:    printf("(%d)", (int)t.value.int_val); break;
        case TOK_DOUBLE_LIT: printf("(%g)", t.value.double_val); break;
        case TOK_STRING_LIT: putchar('('); print_string_value(t.value.str_val); putchar(')'); break;
        default: break;
        }
        if ((flags & SHOW_NL) && t.nl_before)
            putchar('^');
        if (flags & SHOW_LINE)
            printf(":%d", t.line);

        token_free(&t);
    } while (t.type != TOK_EOF);
    putchar('\n');
}

// ---------------------------------------------------------------------------
// Spuštění jednoho testu v samostatném procesu
// ---------------------------------------------------------------------------

#define OUT_MAX 8192

// Vrací návratový kód potomka (nebo -1 při pádu), výstup uloží do out.
static int run_case(const char *input, int flags, char *out, size_t out_size)
{
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
        perror("pipe");
        exit(99);
    }
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(99);
    }
    if (pid == 0) {                         // potomek
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);  close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        if (freopen("/dev/null", "w", stderr) == NULL) { /* hlášky nepotřebujeme */ }
        dump_tokens(flags);
        exit(0);
    }

    // rodič: vstup je malý, vejde se do roury celý
    close(in_pipe[0]);
    close(out_pipe[1]);
    size_t len = strlen(input);
    if (write(in_pipe[1], input, len) != (ssize_t)len) {
        perror("write");
        exit(99);
    }
    close(in_pipe[1]);

    size_t total = 0;
    ssize_t n;
    while ((n = read(out_pipe[0], out + total, out_size - 1 - total)) > 0)
        total += (size_t)n;
    out[total] = '\0';
    if (total > 0 && out[total - 1] == '\n')
        out[total - 1] = '\0';
    close(out_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// ---------------------------------------------------------------------------
// Testovací případy
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;
    const char *input;
    int exit_code;        // 0 = OK, 1 = lexikální chyba
    const char *expected; // očekávané tokeny; NULL = nekontrolovat (u chyb)
    int flags;
} tCase;

#define OK(name, in, exp)        { name, in, 0, exp, 0 }
#define OKF(name, in, exp, fl)   { name, in, 0, exp, fl }
#define LEXERR(name, in)         { name, in, 1, NULL, 0 }
#define SYNERR(name, in)         { name, in, 2, NULL, 0 }

static const tCase cases[] = {
    // --- identifikátory a klíčová slova ---
    OK("identifikator", "abc\n", "ID(abc) EOL EOF"),
    OK("identifikator s cislicemi a _", "a_1 B2c\n", "ID(a_1) ID(B2c) EOL EOF"),
    OK("dve podtrzitka", "__ _a\n", "ID(__) ID(_a) EOL EOF"),
    OK("samotne podtrzitko", "_\n", "_ EOL EOF"),
    OK("podtrzitko a mezera", "x = _ \n", "ID(x) = _ EOL EOF"),
    OK("klicova slova", "def do else if import return then val var while for\n",
       "def do else if import return then val var while for EOL EOF"),
    OK("rezervovana slova", "case class extension match type\n",
       "case class extension match type EOL EOF"),
    OK("nazvy typu", "Int Double String Unit Boolean List\n",
       "Int Double String Unit Boolean List EOL EOF"),
    OK("bool literaly", "true false\n", "true false EOL EOF"),
    OK("velikost pismen", "int Def TRUE\n", "ID(int) ID(Def) ID(TRUE) EOL EOF"),
    OK("to until by jsou identifikatory", "to until by main\n",
       "ID(to) ID(until) ID(by) ID(main) EOL EOF"),
    OK("klicove slovo jako prefix", "define iff values\n",
       "ID(define) ID(iff) ID(values) EOL EOF"),

    // --- celá čísla ---
    OK("int", "42\n", "INT(42) EOL EOF"),
    OK("nula", "0\n", "INT(0) EOL EOF"),
    OK("podtrzitka v int", "1_000_000\n", "INT(1000000) EOL EOF"),
    OK("vice podtrzitek za sebou", "1__0\n", "INT(10) EOL EOF"),
    OK("int a operator", "1+2-3\n", "INT(1) + INT(2) - INT(3) EOL EOF"),
    OK("zaporne cislo je unarni minus", "-5\n", "- INT(5) EOL EOF"),
    // zravy lexer: platny token se vyda, chybu najde parser (2); "01" overit na foru
    LEXERR("uvodni nula", "01\n"),
    LEXERR("nula a podtrzitko", "0_1\n"),
    LEXERR("podtrzitko na konci", "1_\n"),
    LEXERR("podtrzitko pred teckou", "1_.5\n"),
    OK("pismeno za cislem", "12abc\n", "INT(12) ID(abc) EOL EOF"),
    OK("priklad z prednasky", "x = 10abc\"y\"\n", "ID(x) = INT(10) ID(abc) STR(\"y\") EOL EOF"),
    LEXERR("podtrzitko a pismeno", "1_a\n"),

    // --- desetinná čísla ---
    OK("double", "3.14\n", "DBL(3.14) EOL EOF"),
    OK("double s nulou", "0.5\n", "DBL(0.5) EOL EOF"),
    OK("double bez cele casti", ".5\n", "DBL(0.5) EOL EOF"),
    OK("exponent", "1e5 2E3\n", "DBL(100000) DBL(2000) EOL EOF"),
    OK("exponent se znamenkem", "1.5e-3 1e+2\n", "DBL(0.0015) DBL(100) EOL EOF"),
    OK("pripona d/D", "2.5d 1e2D\n", "DBL(2.5) DBL(100) EOL EOF"),
    OK("podtrzitka v double", "1_0.2_5e1_0\n", "DBL(1.025e+11) EOL EOF"),
    OK("nula s exponentem", "0e0 0.0\n", "DBL(0) DBL(0) EOL EOF"),
    OK("double a minus", "1.5-3\n", "DBL(1.5) - INT(3) EOL EOF"),
    LEXERR("e bez cislic", "1e\n"),
    LEXERR("znamenko bez cislic", "1e-\n"),
    LEXERR("dve znamenka", "1e+-2\n"),
    OK("pismeno za double", "1.5x\n", "DBL(1.5) ID(x) EOL EOF"),
    OK("pismeno za exponentem", "1e5x\n", "DBL(100000) ID(x) EOL EOF"),
    OK("dva exponenty", "1e5e3\n", "DBL(100000) ID(e3) EOL EOF"),
    OK("neco za priponou", "1.5dd\n", "DBL(1.5) ID(d) EOL EOF"),
    OK("vice podtrzitek (forum)", "1___000\n", "INT(1000) EOL EOF"),
    OK("exponent s nulami (forum)", "2e001 2e-001\n", "DBL(20) DBL(0.2) EOL EOF"),
    OK("podtrzitko za teckou je clen", "1._5\n", "INT(1) . ID(_5) EOL EOF"),

    // --- tečka ---
    OK("clen hodnoty", "x.length\n", "ID(x) . ID(length) EOL EOF"),
    OK("clen int literalu", "5.toString\n", "INT(5) . ID(toString) EOL EOF"),
    OK("retezeni clenu", "n.floor.toInt\n", "ID(n) . ID(floor) . ID(toInt) EOL EOF"),
    OK("tecka a podtrzitko", "._5\n", ". ID(_5) EOL EOF"),
    OK("dve tecky", "a..b\n", "ID(a) . . ID(b) EOL EOF"),

    // --- operátory ---
    OK("porovnani", "< <= > >= == !=\n", "< <= > >= == != EOL EOF"),
    OK("prirazeni a negace", "= !x\n", "= ! ID(x) EOL EOF"),
    OK("logicke operatory", "a && b || c\n", "ID(a) && ID(b) || ID(c) EOL EOF"),
    OK("plus a konkatenace", "a + b ++ c\n", "ID(a) + ID(b) ++ ID(c) EOL EOF"),
    OK("sipka generatoru", "i <- 1\n", "ID(i) <- INT(1) EOL EOF"),
    OK("maximal munch <-", "x<-1\n", "ID(x) <- INT(1) EOL EOF"),
    OK("operatory bez mezer", "a<=b>=c\n", "ID(a) <= ID(b) >= ID(c) EOL EOF"),
    OK("trojite =", "a===b\n", "ID(a) == = ID(b) EOL EOF"),
    OK("jednoznakove", "* / , : ; ( ) [ ]\n", "* / , : ; ( ) [ ] EOL EOF"),
    LEXERR("samotny &", "a & b\n"),
    LEXERR("samotny |", "a | b\n"),
    LEXERR("neznamy znak #", "a # b\n"),
    LEXERR("neznamy znak $", "$\n"),
    LEXERR("neznamy znak {", "{\n"),
    LEXERR("znak mimo ASCII", "a \xc3\xa1\n"),

    // --- @main ---
    OK("anotace", "@main\ndef f()\n", "@main EOL def ID(f) ( ) EOL EOF"),
    OK("anotace na radku s def", "@main def f()\n", "@main def ID(f) ( ) EOL EOF"),
    LEXERR("mezera za @", "@ main\n"),
    LEXERR("jine slovo za @", "@test\n"),
    LEXERR("delsi slovo za @", "@mainx\n"),
    LEXERR("samotny @", "@\n"),

    // --- řetězce ---
    OK("retezec", "\"abc\"\n", "STR(\"abc\") EOL EOF"),
    OK("prazdny retezec", "\"\" + \"a\"\n", "STR(\"\") + STR(\"a\") EOL EOF"),
    OK("escape sekvence", "\"a\\tb\\\\c\\\"d\\ne\\rf\"\n",
       "STR(\"a\\tb\\\\c\\\"d\\ne\\rf\") EOL EOF"),
    OK("escape na zacatku", "\"\\n\"\n", "STR(\"\\n\") EOL EOF"),
    OK("tabulator v retezci", "\"a\tb\"\n", "STR(\"a\\tb\") EOL EOF"),
    OK("komentar v retezci", "\"// /* x\"\n", "STR(\"// /* x\") EOL EOF"),
    LEXERR("neplatna escape sekvence", "\"a\\x\"\n"),
    LEXERR("neukonceny retezec (EOL)", "\"abc\nx\n"),
    LEXERR("neukonceny retezec (EOF)", "\"abc"),
    LEXERR("zpetne lomitko na konci", "\"abc\\"),

    // --- víceřádkové řetězce ---
    OK("viceradkovy dle spec", "x = \"\"\"A\n  hoj s\\nvete\n  \"\"\"\ny\n",
       "ID(x) = STR(\"A\\n  hoj s\\\\nvete\\n  \") EOL ID(y) EOL EOF"),
    OK("prazdny viceradkovy", "\"\"\"\"\"\"\n", "STR(\"\") EOL EOF"),
    OK("uvozovky uvnitr", "\"\"\"a\"\"b\"\"\"\n", "STR(\"a\\\"\\\"b\") EOL EOF"),
    OK("uvozovka pred koncem", "\"\"\"a\"\"\"\"\n", "STR(\"a\\\"\") EOL EOF"),
    OK("dve uvozovky pred koncem", "\"\"\"a\"\"\"\"\"\n", "STR(\"a\\\"\\\"\") EOL EOF"),
    OK("viceradkovy neni EOL", "f(\"\"\"a\nb\"\"\")\n", "ID(f) ( STR(\"a\\nb\") ) EOL EOF"),
    LEXERR("neukonceny viceradkovy", "\"\"\"abc\n"),
    LEXERR("neukonceny viceradkovy s uvozovkami", "\"\"\"abc\"\""),

    // --- komentáře ---
    OK("radkovy komentar", "a // komentar\nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("radkovy komentar na konci souboru", "a // x", "ID(a) EOL EOF"),
    OK("radek jen s komentarem", "a\n  // x\nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("hvezdicka v radkovem", "a // a*b /* c\nd\n", "ID(a) EOL ID(d) EOL EOF"),
    OK("blokovy jako mezera", "a/*x*/b\n", "ID(a) ID(b) EOL EOF"),
    OK("vnoreny blokovy", "/* a /* b */ c */ x\n", "ID(x) EOL EOF"),
    OK("hvezdicky v komentari", "/** a **/ x\n", "ID(x) EOL EOF"),
    OK("lomitka v komentari", "/* a //* b */ */ x\n", "ID(x) EOL EOF"),
    OK("viceradkovy blokovy = EOL", "a /* x\ny */\nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("blokovy na vlastnich radcich", "a\n/* x\ny */\nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("deleni", "a / b\n", "ID(a) / ID(b) EOL EOF"),
    SYNERR("token za viceradkovym komentarem", "a /* x\ny */ b\n"),
    LEXERR("neukonceny blokovy", "/* abc\n"),
    LEXERR("neukonceny vnoreny", "/* a /* b */ c\n"),

    // --- řádky a EOL ---
    OK("bez koncoveho LF", "a", "ID(a) EOL EOF"),
    OK("prazdny vstup", "", "EOF"),
    OK("jen prazdne radky", "\n\n  \n", "EOF"),
    OK("prazdne radky mezi prikazy", "a\n\n\nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("CRLF", "a\r\nb\r\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("strednik", "a; b\n", "ID(a) ; ID(b) EOL EOF"),

    // --- odsazení ---
    OK("indent a dedent",
       "def f() =\n  x\ny\n",
       "def ID(f) ( ) = EOL INDENT ID(x) EOL DEDENT ID(y) EOL EOF"),
    OK("vnorene bloky",
       "a\n  b\n    c\n  d\ne\n",
       "ID(a) EOL INDENT ID(b) EOL INDENT ID(c) EOL DEDENT ID(d) EOL DEDENT ID(e) EOL EOF"),
    OK("dva dedenty najednou",
       "a\n  b\n    c\nd\n",
       "ID(a) EOL INDENT ID(b) EOL INDENT ID(c) EOL DEDENT DEDENT ID(d) EOL EOF"),
    OK("dedenty na EOF",
       "a\n  b\n    c\n",
       "ID(a) EOL INDENT ID(b) EOL INDENT ID(c) EOL DEDENT DEDENT EOF"),
    OK("dedenty na EOF bez LF",
       "a\n  b",
       "ID(a) EOL INDENT ID(b) EOL DEDENT EOF"),
    OK("prazdny radek nemeni odsazeni",
       "a\n  b\n\n  c\n",
       "ID(a) EOL INDENT ID(b) EOL ID(c) EOL DEDENT EOF"),
    OK("radek s mezerami nemeni odsazeni",
       "a\n  b\n \n  c\n",
       "ID(a) EOL INDENT ID(b) EOL ID(c) EOL DEDENT EOF"),
    OK("komentar s jinym odsazenim",
       "a\n  b\n// x\n      // y\n  c\n",
       "ID(a) EOL INDENT ID(b) EOL ID(c) EOL DEDENT EOF"),
    OK("libovolna sirka odsazeni",
       "a\n   b\n         c\n",
       "ID(a) EOL INDENT ID(b) EOL INDENT ID(c) EOL DEDENT DEDENT EOF"),
    OK("prvni radek odsazeny",
       "  a\n",
       "INDENT ID(a) EOL DEDENT EOF"),
    SYNERR("dedent na neexistujici uroven", "a\n    b\n  c\n"),
    LEXERR("tabulator v odsazeni", "a\n\tb\n"),
    OK("tab na prazdnem radku", "a\n\t\nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("mezery a tab na prazdnem radku", "a\n  \t  \nb\n", "ID(a) EOL ID(b) EOL EOF"),
    OK("tab na poslednim radku pred EOF", "a\n\t", "ID(a) EOL EOF"),
    OK("tab mezi tokeny", "a\tb\n", "ID(a) ID(b) EOL EOF"),
    LEXERR("tab pred komentarem", "a\n\t// k\nb\n"),
    LEXERR("tab na neprazdnem radku v bloku", "a\n  b\n  \tc\n"),
    LEXERR("tabulator za mezerami", "a\n  \tb\n"),

    // --- závorky ---
    OK("LF v zavorkach neni EOL", "f(a,\n  b)\n", "ID(f) ( ID(a) , ID(b) ) EOL EOF"),
    OK("LF v hranatych zavorkach", "x[1,\n  2]\n", "ID(x) [ INT(1) , INT(2) ] EOL EOF"),
    OK("vnorene zavorky", "f(g(a,\n  b),\n  c)\n",
       "ID(f) ( ID(g) ( ID(a) , ID(b) ) , ID(c) ) EOL EOF"),
    OK("prazdny radek v zavorkach", "f(a,\n\n  b)\n", "ID(f) ( ID(a) , ID(b) ) EOL EOF"),
    OK("zavorky v bloku",
       "a\n  f(x,\n    y)\n  b\n",
       "ID(a) EOL INDENT ID(f) ( ID(x) , ID(y) ) EOL ID(b) EOL DEDENT EOF"),
    OK("prebytecna zavorka", "a)\n", "ID(a) ) EOL EOF"),
    SYNERR("radek v zavorkach neodsazeny", "f(a,\nb)\n"),

    // --- nl_before (koncová čárka) ---
    OKF("nl_before v zavorkach", "f(\n  a,\n  )\n",
        "ID(f) ( ID(a)^ , )^ EOL^ EOF^", SHOW_NL),
    // Zavírací závorka na úrovni začátku logického řádku: spec 1.2 doslova říká
    // "následující řádek odsazen více", Scala to ale povoluje. Ověřit na fóru;
    // test popisuje současné chování (chyba 1).
    SYNERR("zaviraci zavorka neodsazena (fórum!)", "f(\n  a,\n)\n"),
    OKF("nl_before bez LF", "f(a, b)\n",
        "ID(f) ( ID(a) , ID(b) ) EOL^ EOF^", SHOW_NL),
    OKF("nl_before po dedentu", "a\n  b\nc\n",
        "ID(a) EOL^ INDENT^ ID(b)^ EOL^ DEDENT^ ID(c)^ EOL^ EOF^", SHOW_NL),

    // --- čísla řádků ---
    OKF("cisla radku", "a\n\nb c\n",
        "ID(a):1 EOL:1 ID(b):3 ID(c):3 EOL:3 EOF:4", SHOW_LINE),
    OKF("radek viceradkoveho retezce", "\"\"\"a\nb\"\"\" x\n",
        "STR(\"a\\nb\"):1 ID(x):2 EOL:2 EOF:3", SHOW_LINE),

    // --- celý program ---
    OK("ukazkovy program",
       "import ifj26.*\n"
       "\n"
       "@main\n"
       "def main(): Unit =\n"
       "  val n = readInt() // vstup\n"
       "  /* vypocet\n"
       "     faktorialu */\n"
       "  var r: Int = 1\n"
       "  while n > 0 do\n"
       "    r = r * n\n"
       "    n = n - 1\n"
       "  if r != 0 then\n"
       "    print(r.toString ++ \"\\n\")\n",
       "import ID(ifj26) . * EOL "
       "@main EOL "
       "def ID(main) ( ) : Unit = EOL "
       "INDENT val ID(n) = ID(readInt) ( ) EOL "
       "var ID(r) : Int = INT(1) EOL "
       "while ID(n) > INT(0) do EOL "
       "INDENT ID(r) = ID(r) * ID(n) EOL "
       "ID(n) = ID(n) - INT(1) EOL "
       "DEDENT if ID(r) != INT(0) then EOL "
       "INDENT ID(print) ( ID(r) . ID(toString) ++ STR(\"\\n\") ) EOL "
       "DEDENT DEDENT EOF"),
};
#define CASE_COUNT (sizeof(cases) / sizeof(cases[0]))

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
    bool verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    static char out[OUT_MAX];
    int failed = 0;

    for (size_t i = 0; i < CASE_COUNT; i++) {
        const tCase *tc = &cases[i];
        int code = run_case(tc->input, tc->flags, out, sizeof(out));

        bool ok = (code == tc->exit_code)
               && (tc->expected == NULL || strcmp(out, tc->expected) == 0);

        if (!ok) {
            failed++;
            printf("FAIL  %s\n", tc->name);
            printf("      kod:       %d (cekano %d)\n", code, tc->exit_code);
            if (tc->expected != NULL)
                printf("      cekano:    %s\n", tc->expected);
            printf("      dostal:    %s\n", out);
        } else if (verbose) {
            printf("ok    %-40s %s\n", tc->name, out);
        }
    }

    printf("\n%zu testu, %d selhalo\n", CASE_COUNT, failed);
    return failed == 0 ? 0 : 1;
}
