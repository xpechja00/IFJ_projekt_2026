CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g -MMD -MP
TARGET  := compiler

SRCS    := $(wildcard *.c)
OBJS    := $(SRCS:.c=.o)

.PHONY: build check build-check test format clean

build: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

check:
	clang-format --dry-run --Werror *.c *.h
	cppcheck --enable=warning,style --std=c11 --quiet --error-exitcode=1 .

build-check: check build

format:
	clang-format -i *.c *.h

test: $(TARGET) tests/scanner_test tests/symtable_test
	./tests/scanner_test
	./tests/symtable_test
	sh tests/run_tests.sh

tests/scanner_test: tests/scanner_test.c scanner.o error.o
	$(CC) $(CFLAGS) -I. $^ -o $@

tests/symtable_test: tests/test_symtable.c symtable.o
	$(CC) $(CFLAGS) -I. $^ -o $@

clean:
	rm -f $(TARGET) *.o *.d tests/scanner_test tests/symtable_test tests/*.d

-include $(OBJS:.o=.d)