# C Minor Compiler

A student-owned compiler for the restricted C Minor language in the course SRS 1.0.
The compiler implementation is C++14. It contains its own Flex lexer, recursive-descent
parser, AST, semantic analyzer, symbol tables, three-address IR, and LLVM IR emitter.
LLVM `llc` is used only for machine-code generation; GCC assembles generated `.s`
files and links generated `.o` files.

This is not a general C compiler. It does not preprocess source and must never pass
`.cm` or C Minor `.c` input to GCC or Clang.

## Required environment

The acceptance target is Linux x86-64 / System V AMD64. Use Ubuntu or WSL2 with
the public dependencies installed:

```bash
sudo apt update
sudo apt install build-essential make python3 flex nodejs npm llvm
```

The SRS reference backend is LLVM 23.1.3. If the distribution provides another
LLVM release, record its exact version and test compatibility.

## Build and tests

```bash
make
make test
```

The scanner is written in `src/lexer.l`; Make runs Flex to generate the scanner
source in the ignored `build/` directory before compiling the compiler.

`make test` runs source-level positive, negative, and malformed-input cases. It
includes at least 50 distinct positive/negative cases and 100 malformed or mutated
inputs, each with a five-second timeout.

Run the end-to-end execution suite on Linux with `llc` and GCC available:

```bash
make test-execution
```

That suite sends C Minor input through the student compiler, asks `llc` for
assembly, then gives only the generated `.s` and `.o` files to GCC:

```bash
./build/cminorcc --backend=llvm examples/basic.cm -o examples/basic.s
gcc -c examples/basic.s -o examples/basic.o
gcc -no-pie examples/basic.o -o examples/basic
./examples/basic
printf 'exit=%s\n' "$?"
```

On Windows, the C++ compiler can be built with a working MSYS2 UCRT64 toolchain,
but the required Linux assembly/link/run acceptance still needs WSL2 or Linux.
`llc` must be on `PATH` for native compilation.

In an MSYS2 UCRT64 terminal, install the build tools with:

```bash
pacman -S --needed flex make
```

For frontend work in a PowerShell terminal with MSYS2 UCRT64, Flex, and Make installed:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:PATH
make
python tests\run_tests.py build\cminorcc.exe
```

## Browser test interface

The local React interface exercises the actual compiler executable and its existing
test suite. Compiler stages are run by the C++ executable, not imitated in JavaScript.
On Windows or Linux x86-64, **Run program** builds and runs a host-native program.
Node.js serves the interface on loopback and launches tools with argument arrays
(not through a shell).

```bash
npm --prefix ui install
npm --prefix ui run dev
```

This starts the React development server and compiler API together. Leave this
terminal open and visit `http://127.0.0.1:4173`. Use the source editor and **Run
program** to compile, link, and execute; Windows uses a Windows-native object and
Linux uses Linux x86-64 assembly. The result shows the program's captured
stdout/stderr and its real exit status. The compiler supports string and single-byte
character literals, plus the built-in variadic `printf` function with a format
literal and integer arguments. Use the separate **Compiler stage**
selection to inspect stages 1-7, or **Run test suite** to execute
`tests/run_tests.py`. Program execution requires `llc` and GCC. On Windows, use
LLVM with the Windows x86-64 target and MinGW-w64 GCC. Stage 7 continues to output
Linux x86-64 assembly; install LLVM to enable stage 7 assembly output.
Install LLVM on Windows with `winget install --id LLVM.LLVM -e`, or use the LLVM
installer. In MSYS2 UCRT64, install the tested LLVM version with
`pacman -S mingw-w64-ucrt-x86_64-llvm`. The UI automatically adds
`C:\Program Files\LLVM\bin` and `C:\msys64\ucrt64\bin` when present. If LLVM
is installed elsewhere, set `C_MINOR_LLVM_PATH` to the directory containing `llc.exe`
before starting the UI. Restart the UI after installing LLVM or changing `PATH`.
On Windows, the UI server automatically adds the standard MSYS2 UCRT64 runtime
directories when they exist. If you installed MSYS2 elsewhere, set
`C_MINOR_RUNTIME_PATH` to its runtime `bin` directory before starting the UI.
The header displays `run-ready` when the required compiler, `llc`, and GCC are
available.
Do not open `ui/index.html` directly as a file: it shows startup instructions;
the compiler API must be running to process source and inspect compiler stages.

For production serving instead, build the bundle with `npm --prefix ui run build`,
then run `npm --prefix ui start`.

## Compiler commands

```bash
./build/cminorcc --help
./build/cminorcc --version
./build/cminorcc --capabilities
./build/cminorcc --dump-tokens examples/basic.cm
./build/cminorcc --dump-ast examples/basic.cm
./build/cminorcc --dump-symbols examples/basic.cm
./build/cminorcc --dump-ir examples/basic.cm
./build/cminorcc --check examples/basic.cm
./build/cminorcc --emit-backend-ir examples/basic.cm -o examples/basic.ll
./build/cminorcc --backend=llvm examples/basic.cm -o examples/basic.s
```

`--check` runs lexical, syntax, and semantic analysis without invoking the native
backend. `--dump-ir` displays the compiler's own IR, which is distinct from LLVM IR.
The normal compile command emits GNU-compatible x86-64 assembly.

## Project layout

- `include/cminor/frontend.hpp`: token, AST, parser, and semantic-analysis interfaces.
- `include/cminor/ir.hpp`: student IR structures and lowering interfaces.
- `src/lexer.l`: Flex lexer rules, diagnostics, and source-location tracking.
- `src/lexer_support.cpp`: shared diagnostic and token-dump support.
- `src/parser.cpp`: handwritten recursive-descent parser and AST construction.
- `src/semantic.cpp`: function signatures, scopes, type/category checks, and name resolution.
- `src/ir.cpp`: AST-to-student-IR lowering, student IR dump, and LLVM IR generation.
- `src/main.cpp`: CLI, diagnostics, output handling, and safe external backend invocation.
- `tests/`: positive/negative cases and the timeout-based test runner.
- `ui/`: React browser interface and local Node.js compiler bridge.
- `docs/STATUS.md`: current implementation and acceptance status.

GNU Flex generates the scanner from the project's own `src/lexer.l` specification.
The parser remains a handwritten recursive-descent C++ parser as requested.

## Current acceptance status

The compiler frontend and IR lowering are implemented, and `make test` covers
positive, negative, and malformed input. End-to-end Linux verification is still
required before this project can be called SRS-complete: LLVM IR must be accepted
by the selected `llc`, generated assembly must assemble/link, and execution results
must match the normative examples. See [docs/STATUS.md](./docs/STATUS.md).
