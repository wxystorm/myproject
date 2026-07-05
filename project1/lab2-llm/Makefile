CC      ?= cc
CFLAGS  := -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Werror -pedantic -g -Isrc

ASAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

BIN  := llm
SRCS := src/llm.c

.PHONY: all test test-a test-b asan test-asan clean \

all: $(BIN)

$(BIN): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $@

test: $(BIN)
	python3 tests/run_tests.py --bin ./$(BIN)

test-a test-b: $(BIN)
	python3 tests/run_tests.py --bin ./$(BIN) --phase $(@:test-%=%)

asan: $(SRCS)
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $(SRCS) -o $(BIN)-asan
	python3 tests/run_tests.py --bin ./$(BIN)-asan

test-asan: asan

clean:
	rm -f $(BIN) $(BIN)-asan $(BIN)-ref
	find . -name '*.dSYM' -type d -exec rm -rf {} + 2>/dev/null || true
