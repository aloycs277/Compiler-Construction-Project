#include "cminor/frontend.hpp"
#include "cminor/ir.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

enum class Mode {
    None, Check, DumpTokens, DumpAst, DumpSymbols, DumpIR, EmitBackendIR
};

struct ProcessResult {
    bool started;
    int status;
    std::string output;
    ProcessResult() : started(false), status(-1) {}
};

std::string readSource(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input) {
        throw cminor::Diagnostic("IO_ERROR", cminor::SourceLocation{path, 0, 0},
                                 "cannot open input file");
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        throw cminor::Diagnostic("IO_ERROR", cminor::SourceLocation{path, 0, 0},
                                 "failed while reading input file");
    }
    return contents.str();
}

bool hasSourceExtension(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) return false;
    const std::string extension = path.substr(dot);
    return extension == ".cm" || extension == ".c";
}

std::string defaultAssemblyPath(const std::string& sourcePath) {
    const std::size_t slash = sourcePath.find_last_of("/\\");
    const std::size_t dot = sourcePath.find_last_of('.');
    if (dot == std::string::npos ||
        (slash != std::string::npos && dot < slash)) return sourcePath + ".s";
    return sourcePath.substr(0, dot) + ".s";
}

std::string quoteArgument(const std::string& argument) {
    std::string result = "\"";
    for (std::size_t i = 0; i < argument.size(); ++i) {
        if (argument[i] == '"' || argument[i] == '\\') result += '\\';
        result += argument[i];
    }
    result += '"';
    return result;
}

std::string commandText(const std::vector<std::string>& arguments) {
    std::ostringstream out;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i) out << ' ';
        out << quoteArgument(arguments[i]);
    }
    return out.str();
}

ProcessResult runProcess(const std::vector<std::string>& arguments) {
    ProcessResult result;
    if (arguments.empty()) return result;
#ifdef _WIN32
    std::vector<const char*> argv;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        argv.push_back(arguments[i].c_str());
    }
    argv.push_back(NULL);
    const intptr_t status = _spawnvp(_P_WAIT, arguments[0].c_str(), &argv[0]);
    if (status != -1) {
        result.started = true;
        result.status = static_cast<int>(status);
    }
#else
    std::vector<char*> argv;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        argv.push_back(const_cast<char*>(arguments[i].c_str()));
    }
    argv.push_back(NULL);
    const pid_t child = fork();
    if (child == 0) {
        execvp(argv[0], &argv[0]);
        _exit(127);
    }
    if (child < 0) return result;
    result.started = true;
    int waitStatus = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &waitStatus, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.started = false;
        return result;
    }
    result.status = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : 128;
#endif
    return result;
}

ProcessResult captureProcess(const std::vector<std::string>& arguments) {
    ProcessResult result;
    if (arguments.empty()) return result;
#ifdef _WIN32
    const std::string fixedCommand = "llc --version 2>&1";
    FILE* pipe = _popen(fixedCommand.c_str(), "r");
    if (pipe == NULL) return result;
    result.started = true;
    char buffer[512];
    while (std::fgets(buffer, sizeof buffer, pipe) != NULL) {
        result.output += buffer;
    }
    const int status = _pclose(pipe);
    result.status = status;
#else
    int descriptors[2];
    if (pipe(descriptors) != 0) return result;
    std::vector<char*> argv;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        argv.push_back(const_cast<char*>(arguments[i].c_str()));
    }
    argv.push_back(NULL);
    const pid_t child = fork();
    if (child == 0) {
        close(descriptors[0]);
        dup2(descriptors[1], STDOUT_FILENO);
        dup2(descriptors[1], STDERR_FILENO);
        close(descriptors[1]);
        execvp(argv[0], &argv[0]);
        _exit(127);
    }
    close(descriptors[1]);
    if (child < 0) {
        close(descriptors[0]);
        return result;
    }
    result.started = true;
    char buffer[512];
    ssize_t count;
    while ((count = read(descriptors[0], buffer, sizeof buffer)) > 0) {
        result.output.append(buffer, static_cast<std::size_t>(count));
    }
    close(descriptors[0]);
    int waitStatus = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &waitStatus, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        result.started = false;
        return result;
    }
    result.status = WIFEXITED(waitStatus) ? WEXITSTATUS(waitStatus) : 128;
#endif
    return result;
}

std::string processIdText() {
#ifdef _WIN32
    std::ostringstream out;
    out << _getpid();
#else
    std::ostringstream out;
    out << getpid();
#endif
    return out.str();
}

std::string temporaryPath(const std::string& destination,
                          const std::string& extension) {
    static unsigned sequence = 0;
    std::ostringstream path;
    path << destination << ".cminor-tmp-" << processIdText() << '-'
         << sequence++ << extension;
    return path.str();
}

bool writeFile(const std::string& path, const std::string& contents,
               std::string& error) {
    std::ofstream output(path.c_str(), std::ios::out | std::ios::binary);
    if (!output) {
        error = "cannot create '" + path + "'";
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
        error = "failed while writing '" + path + "'";
        output.close();
        std::remove(path.c_str());
        return false;
    }
    return true;
}

bool replaceFile(const std::string& temporary, const std::string& destination,
                 std::string& error) {
#ifdef _WIN32
    if (!MoveFileExA(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::ostringstream message;
        message << "cannot replace '" << destination
                << "' (Windows error " << GetLastError() << ')';
        error = message.str();
        return false;
    }
#else
    if (std::rename(temporary.c_str(), destination.c_str()) != 0) {
        error = "cannot replace '" + destination + "': " + std::strerror(errno);
        return false;
    }
#endif
    return true;
}

void printUsage() {
    std::cout
        << "cminorcc [--backend=llvm] [--check|--dump-tokens|--dump-ast|"
           "--dump-symbols|--dump-ir|--emit-backend-ir] input.cm [-o output]\n"
        << "       cminorcc --help|--version|--capabilities\n";
}

int commandError(const std::string& message) {
    std::cerr << "error[COMMAND_ERROR]: " << message << '\n';
    return 2;
}

void printVersion() {
    std::cout << "cminorcc 0.2.0 (C Minor SRS 1.0; C++14; LLVM backend)\n";
}

void printCapabilities() {
    std::cout << "Language: C Minor SRS 1.0\n"
              << "Implementation: C++14 (" << __VERSION__
              << "), GNU Flex lexer and recursive-descent parser\n"
              << "Target: Linux x86-64, System V AMD64 ABI\n"
              << "Backend: LLVM llc\n";
    const ProcessResult version = captureProcess(std::vector<std::string>{"llc", "--version"});
    if (version.started && version.status == 0) {
        std::istringstream lines(version.output);
        std::string line;
        std::string reportedVersion;
        std::string firstLine;
        while (std::getline(lines, line)) {
            if (firstLine.empty() && !line.empty()) firstLine = line;
            if (line.find("version") != std::string::npos ||
                line.find("Version") != std::string::npos) {
                reportedVersion = line;
                break;
            }
        }
        std::cout << "LLVM version: "
                  << (reportedVersion.empty() ? firstLine : reportedVersion)
                  << '\n';
    } else {
        std::cout << "LLVM version: llc not found or failed to run\n";
    }
    std::cout << "Sources: one self-contained .cm or .c file\n"
              << "Minimum tested support: 32 KiB, 4,000 tokens, 16 nested blocks "
       "and expressions, 32 functions, and 2,048-character lines\n"
              << "Limits: 0..6 int parameters, 64 local declarations per block, "
       "256-level parser nesting guard\n"
    << "Supported literals: decimal integers, byte characters, and strings "
       "as printf format arguments\n"
    << "Built-ins: variadic printf(format, integer arguments)\n"
    << "Excluded: globals, pointers, arrays, structs, arbitrary external calls, "
       "and non-C-Minor C constructs\n"
    << "Preprocessing: disabled; directives and line splicing rejected\n"
              << "Optional extensions: printf and string/character literals\n";
}

} // namespace

int main(int argc, char** argv) {
    Mode mode = Mode::None;
    std::string inputPath;
    std::string outputPath;
    bool backendSelected = false;
    bool outputSelected = false;
    bool informational = false;
    enum InfoMode { NoInfo, Help, Version, Capabilities } infoMode = NoInfo;

    for (int i = 1; i < argc; ++i) {
        const std::string argument(argv[i]);
        if (argument == "--help") {
            informational = true;
            infoMode = Help;
        } else if (argument == "--version") {
            informational = true;
            infoMode = Version;
        } else if (argument == "--capabilities") {
            informational = true;
            infoMode = Capabilities;
        } else if (argument == "--check") {
            if (mode != Mode::None) return commandError("compiler actions are mutually exclusive");
            mode = Mode::Check;
        } else if (argument == "--dump-tokens") {
            if (mode != Mode::None) return commandError("compiler actions are mutually exclusive");
            mode = Mode::DumpTokens;
        } else if (argument == "--dump-ast") {
            if (mode != Mode::None) return commandError("compiler actions are mutually exclusive");
            mode = Mode::DumpAst;
        } else if (argument == "--dump-symbols") {
            if (mode != Mode::None) return commandError("compiler actions are mutually exclusive");
            mode = Mode::DumpSymbols;
        } else if (argument == "--dump-ir") {
            if (mode != Mode::None) return commandError("compiler actions are mutually exclusive");
            mode = Mode::DumpIR;
        } else if (argument == "--emit-backend-ir") {
            if (mode != Mode::None) return commandError("compiler actions are mutually exclusive");
            mode = Mode::EmitBackendIR;
        } else if (argument.compare(0, 10, "--backend=") == 0) {
            if (backendSelected) return commandError("--backend may only be specified once");
            backendSelected = true;
            if (argument != "--backend=llvm") {
                return commandError("only --backend=llvm is implemented");
            }
        } else if (argument == "-o") {
            if (outputSelected) return commandError("-o may only be specified once");
            if (i + 1 >= argc) return commandError("-o requires a path");
            outputSelected = true;
            outputPath = argv[++i];
        } else if (!argument.empty() && argument[0] == '-') {
            return commandError("unknown option '" + argument + "'");
        } else if (inputPath.empty()) {
            inputPath = argument;
        } else {
            return commandError("exactly one source file is accepted");
        }
    }

    if (informational) {
        if (argc != 2) return commandError("information options cannot be combined with other options");
        if (infoMode == Help) printUsage();
        else if (infoMode == Version) printVersion();
        else printCapabilities();
        return 0;
    }
    if (inputPath.empty()) return commandError("a source file is required");
    if (outputSelected && outputPath.empty()) return commandError("-o requires a non-empty path");
    if (!hasSourceExtension(inputPath)) {
        return commandError("input must end in .cm or .c");
    }
    if (outputSelected && mode != Mode::EmitBackendIR && mode != Mode::None) {
        return commandError("-o is accepted only with normal compilation or --emit-backend-ir");
    }

    try {
        const std::string source = readSource(inputPath);
        const std::vector<cminor::Token> tokens = cminor::lex(source, inputPath);
        if (mode == Mode::DumpTokens) {
            std::cout << cminor::dumpTokens(tokens);
            return 0;
        }

        cminor::Parser parser(tokens);
        cminor::Program program = parser.parse();
        if (mode == Mode::DumpAst) {
            std::cout << cminor::dumpAst(program);
            return 0;
        }

        const cminor::SemanticResult symbols = cminor::analyze(program);
        if (mode == Mode::Check) return 0;
        if (mode == Mode::DumpSymbols) {
            std::cout << cminor::dumpSymbols(symbols);
            return 0;
        }

        const cminor::IRProgram ir = cminor::lowerToIR(program);
        if (mode == Mode::DumpIR) {
            std::cout << cminor::dumpIR(ir);
            return 0;
        }
        const std::string backendIR = cminor::emitLLVM(ir);
        if (mode == Mode::EmitBackendIR) {
            if (!outputSelected) {
                std::cout << backendIR;
                return 0;
            }
            const std::string temporary = temporaryPath(outputPath, ".ll");
            std::string error;
            if (!writeFile(temporary, backendIR, error)) {
                std::cerr << "error[IO_ERROR]: " << error << '\n';
                return 1;
            }
            if (!replaceFile(temporary, outputPath, error)) {
                std::remove(temporary.c_str());
                std::cerr << "error[IO_ERROR]: " << error << '\n';
                return 1;
            }
            return 0;
        }

        const std::string assemblyPath = outputSelected
            ? outputPath : defaultAssemblyPath(inputPath);
        const std::string temporaryIR = temporaryPath(assemblyPath, ".ll");
        const std::string temporaryAssembly = temporaryPath(assemblyPath, ".s");
        std::string error;
        if (!writeFile(temporaryIR, backendIR, error)) {
            std::cerr << "error[IO_ERROR]: " << error << '\n';
            return 1;
        }
        const std::vector<std::string> arguments = {
            "llc", "-O0", "-mtriple=x86_64-pc-linux-gnu",
            "-filetype=asm", temporaryIR, "-o", temporaryAssembly
        };
        const ProcessResult backend = runProcess(arguments);
        std::remove(temporaryIR.c_str());
        if (!backend.started || backend.status != 0) {
            std::remove(temporaryAssembly.c_str());
            std::cerr << "error[BACKEND_ERROR]: failed to run "
                      << commandText(arguments);
            if (backend.started) std::cerr << " (exit status " << backend.status << ')';
            else std::cerr << " (" << std::strerror(errno) << ')';
            std::cerr << '\n';
            return 1;
        }
        if (!replaceFile(temporaryAssembly, assemblyPath, error)) {
            std::remove(temporaryAssembly.c_str());
            std::cerr << "error[IO_ERROR]: " << error << '\n';
            return 1;
        }
        return 0;
    } catch (const cminor::Diagnostic& diagnostic) {
        if (!diagnostic.location.path.empty()) {
            std::cerr << diagnostic.location.path;
            if (diagnostic.location.line > 0) {
                std::cerr << ':' << diagnostic.location.line << ':'
                          << diagnostic.location.column;
            }
            std::cerr << ": ";
        }
        std::cerr << "error[" << diagnostic.code << "]: "
                  << diagnostic.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "error[IO_ERROR]: " << error.what() << '\n';
        return 1;
    }
}
