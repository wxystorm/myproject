CC      ?= cc
CFLAGS  := -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pedantic -g

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

BIN := shell

.PHONY: all test test-a test-b test-c asan test-asan clean 

all: $(BIN)

$(BIN): src/shell.c
	$(CC) $(CFLAGS) $< -o $@

test: $(BIN)
	python3 tests/run_tests.py --bin ./$(BIN)

test-a test-b test-c: $(BIN)
	python3 tests/run_tests.py --bin ./$(BIN) --phase $(@:test-%=%)

asan: src/shell.c
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $< -o $(BIN)-asan
	python3 tests/run_tests.py --bin ./$(BIN)-asan

test-asan: asan

clean:
	rm -f $(BIN) $(BIN)-asan $(BIN)-ref
	find . -name '*.dSYM' -type d -exec rm -rf {} + 2>/dev/null || true
