CXX := g++
CXXFLAGS := -std=c++14 -Wall -Wextra -Wpedantic -O0 -g
FLEX ?= flex
SRC := src/main.cpp src/lexer_support.cpp src/parser.cpp src/semantic.cpp src/ir.cpp src/ast_dump.cpp
LEXER_SPEC := src/lexer.l
LEXER_GEN := build/lexer_generated.cpp
ifeq ($(OS),Windows_NT)
BIN := build/cminorcc.exe
else
BIN := build/cminorcc
endif

.PHONY: all clean test test-execution install ui-build ui-test ui
all: $(BIN)

build:
	mkdir -p build

$(LEXER_GEN): $(LEXER_SPEC) | build
	$(FLEX) --outfile=$@ $<

$(BIN): $(SRC) $(LEXER_GEN) include/cminor/frontend.hpp include/cminor/ir.hpp
	$(CXX) $(CXXFLAGS) -Iinclude $(SRC) $(LEXER_GEN) -o $(BIN)

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/cminorcc

clean:
	rm -rf build/*

test: $(BIN)
	python3 tests/run_tests.py ./$(BIN)

test-execution: $(BIN)
	python3 tests/run_tests.py ./$(BIN) --execution

ui-build:
	npm --prefix ui run build

ui-test: $(BIN)
	npm --prefix ui test

ui: $(BIN) ui-build
	npm --prefix ui start
