#!/bin/sh
# Projekt: Implementace překladače imperativního jazyka IFJ26
# Skript:  run_tests.sh - spustí testovací programy a porovná návratové kódy
#
# Autor: xpechja00
#
# Skript i testovací programy v tests/programy vytvořeny s pomocí AI (Claude).
# Prompt: "napis testy pro parser a semantiku vcetne rozsireni a skript na jejich spusteni".
#
# Použití (z kořene repozitáře, po make):
#   sh tests/run_tests.sh              # jen návratové kódy
#   sh tests/run_tests.sh --valgrind   # navíc kontrola paměti Valgrindem

DIR="$(dirname "$0")/programy"
COMPILER="./compiler"
VALGRIND=0
[ "$1" = "--valgrind" ] && VALGRIND=1

if [ ! -x "$COMPILER" ]; then
    echo "Nenalezen $COMPILER, nejdřív spusť make." >&2
    exit 1
fi

pass=0
fail=0
while read -r name expected; do
    [ -z "$name" ] && continue
    file="$DIR/$name.ifj"
    if [ "$VALGRIND" -eq 1 ]; then
        valgrind -q --leak-check=full --error-exitcode=77 "$COMPILER" < "$file" > /dev/null 2>&1
    else
        "$COMPILER" < "$file" > /dev/null 2>&1
    fi
    got=$?
    if [ "$got" -eq "$expected" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        if [ "$got" -eq 77 ]; then
            echo "FAIL $name: chyba paměti (Valgrind)"
        else
            echo "FAIL $name: očekáváno $expected, vráceno $got"
        fi
    fi
done < "$DIR/expected.txt"

echo "Prošlo $pass z $((pass + fail)) testů."
[ "$fail" -eq 0 ]
