#include "cminor/frontend.hpp"

#include <map>
#include <sstream>

namespace cminor {
namespace {

struct FunctionInfo {
    std::string name;
    bool returnsVoid;
    std::size_t parameterCount;
    bool defined;
    int symbolId;
    SourceLocation location;
    std::string signature;
};

struct LocalInfo {
    int id;
    SourceLocation location;
};

struct Scope {
    Scope* parent;
    int id;
    int depth;
    unsigned declarations;
    std::map<std::string, LocalInfo> locals;
};

class Analyzer {
public:
    SemanticResult run(Program& program) {
        for (std::size_t i = 0; i < program.functions.size(); ++i) {
            analyzeFunction(program.functions[i]);
        }

        const std::map<std::string, FunctionInfo>::const_iterator main =
            functions_.find("main");
        if (main == functions_.end() || !main->second.defined ||
            main->second.returnsVoid || main->second.parameterCount != 0) {
            SourceLocation location;
            if (!program.functions.empty()) location = program.functions[0].span.begin;
            throw Diagnostic("RETURN_ERROR", location,
                             "the source must define exactly one int main(void)");
        }

        for (std::map<std::string, FunctionInfo>::const_iterator it =
                 functions_.begin(); it != functions_.end(); ++it) {
            if (!it->second.defined) {
                throw Diagnostic("NAME_ERROR", it->second.location,
                                 "function '" + it->first +
                                     "' is declared but has no definition");
            }
        }
        return result_;
    }

private:
    static std::string signatureText(const Function& function) {
        std::ostringstream out;
        out << (function.returnsVoid ? "void " : "int ") << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i != 0) out << ',';
            out << "int";
        }
        if (function.parameters.empty()) out << "void";
        out << ')';
        return out.str();
    }

    int addSymbol(const std::string& name, const std::string& kind,
                  const std::string& signature, int scopeId, int depth,
                  const SourceLocation& location) {
        SymbolRecord record;
        record.id = nextSymbolId_++;
        record.name = name;
        record.kind = kind;
        record.signature = signature;
        record.scopeId = scopeId;
        record.depth = depth;
        record.location = location;
        result_.symbols.push_back(record);
        return record.id;
    }

    void addResolution(const std::string& name, const std::string& kind,
                       int targetId, const SourceLocation& location) {
        ResolutionRecord record;
        record.name = name;
        record.kind = kind;
        record.targetId = targetId;
        record.location = location;
        result_.resolutions.push_back(record);
    }

    void analyzeFunction(Function& function) {
        if (function.name == "printf") {
            throw Diagnostic("NAME_ERROR", function.span.begin,
                             "'printf' is a built-in variadic function");
        }
        const std::string signature = signatureText(function);
        std::map<std::string, FunctionInfo>::iterator found =
            functions_.find(function.name);
        if (found == functions_.end()) {
            FunctionInfo info;
            info.name = function.name;
            info.returnsVoid = function.returnsVoid;
            info.parameterCount = function.parameters.size();
            info.defined = false;
            info.location = function.span.begin;
            info.signature = signature;
            info.symbolId = addSymbol(function.name, "function", signature, 0, 0,
                                      function.span.begin);
            found = functions_.insert(std::make_pair(function.name, info)).first;
        } else if (found->second.returnsVoid != function.returnsVoid ||
                   found->second.parameterCount != function.parameters.size()) {
            throw Diagnostic("CALL_ERROR", function.span.begin,
                             "declaration of '" + function.name +
                                 "' is incompatible with " + found->second.signature);
        }
        function.symbolId = found->second.symbolId;
        visibleFunctions_[function.name] = found->second;

        if (!function.definition) return;
        if (found->second.defined) {
            throw Diagnostic("NAME_ERROR", function.span.begin,
                             "function '" + function.name +
                                 "' has more than one definition");
        }
        found->second.defined = true;
        visibleFunctions_[function.name] = found->second;

        if (function.name == "main" &&
            (function.returnsVoid || !function.parameters.empty())) {
            throw Diagnostic("RETURN_ERROR", function.span.begin,
                             "main must have the exact signature int main(void)");
        }
        if (!function.returnsVoid) {
            if (function.body->statements.empty() ||
                function.body->statements.back()->kind != StmtKind::Return ||
                !function.body->statements.back()->first) {
                throw Diagnostic("RETURN_ERROR", function.body->span.begin,
                                 "an int function must end with a top-level return expression");
            }
        }

        Scope functionScope;
        functionScope.parent = NULL;
        functionScope.id = nextScopeId_++;
        functionScope.depth = 0;
        functionScope.declarations = 0;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            Parameter& parameter = function.parameters[i];
            declareLocal(functionScope, parameter.name, parameter.location,
                         "parameter", parameter.localId);
        }
        analyzeStatement(*function.body, functionScope, 0, true,
                         function.returnsVoid);
    }

    void declareLocal(Scope& scope, const std::string& name,
                      const SourceLocation& location, const std::string& kind,
                      int& symbolId) {
        if (name.empty()) {
            throw Diagnostic("SYNTAX_ERROR", location,
                             "parameter name is required in a definition");
        }
        if (scope.locals.find(name) != scope.locals.end()) {
            throw Diagnostic("NAME_ERROR", location,
                             "duplicate declaration of '" + name +
                                 "' in the same scope");
        }
        if (kind == "local" && scope.declarations >= 64) {
            throw Diagnostic("LIMIT_ERROR", location,
                             "a scope may contain at most 64 local declarations");
        }
        if (kind == "local") ++scope.declarations;
        symbolId = addSymbol(name, kind, "", scope.id, scope.depth, location);
        LocalInfo local;
        local.id = symbolId;
        local.location = location;
        scope.locals.insert(std::make_pair(name, local));
    }

    const LocalInfo* findLocal(const Scope& scope, const std::string& name) const {
        for (const Scope* current = &scope; current != NULL;
             current = current->parent) {
            std::map<std::string, LocalInfo>::const_iterator found =
                current->locals.find(name);
            if (found != current->locals.end()) return &found->second;
        }
        return NULL;
    }

    void requireInt(Expr& expression, const Scope& scope) {
        analyzeExpression(expression, scope, false);
        if (expression.isVoid) {
            throw Diagnostic("TYPE_ERROR", expression.span.begin,
                             "void-valued expression cannot be used as an int value");
        }
    }

    void analyzeExpression(Expr& expression, const Scope& scope,
                           bool allowVoidResult) {
        expression.isVoid = false;
        switch (expression.kind) {
        case ExprKind::Integer:
            return;
        case ExprKind::String:
            throw Diagnostic("TYPE_ERROR", expression.span.begin,
                             "string literals are only supported as the first argument to printf");
        case ExprKind::Variable: {
            const LocalInfo* local = findLocal(scope, expression.name);
            if (local == NULL) {
                if (visibleFunctions_.find(expression.name) != visibleFunctions_.end()) {
                    throw Diagnostic("TYPE_ERROR", expression.span.begin,
                                     "function '" + expression.name +
                                         "' is not an int variable");
                }
                throw Diagnostic("NAME_ERROR", expression.span.begin,
                                 "use of undeclared variable '" + expression.name + "'");
            }
            expression.localId = local->id;
            addResolution(expression.name, "variable", local->id,
                          expression.span.begin);
            return;
        }
        case ExprKind::Call: {
            if (findLocal(scope, expression.name) != NULL) {
                throw Diagnostic("TYPE_ERROR", expression.span.begin,
                                 "local variable '" + expression.name +
                                     "' is not callable");
            }
            if (expression.name == "printf" &&
                visibleFunctions_.find(expression.name) == visibleFunctions_.end()) {
                if (expression.arguments.empty() ||
                    expression.arguments[0]->kind != ExprKind::String) {
                    throw Diagnostic("CALL_ERROR", expression.span.begin,
                                     "printf requires a string literal as its first argument");
                }
                for (std::size_t i = 1; i < expression.arguments.size(); ++i) {
                    requireInt(*expression.arguments[i], scope);
                }
                expression.isVoid = false;
                return;
            }
            std::map<std::string, FunctionInfo>::const_iterator function =
                visibleFunctions_.find(expression.name);
            if (function == visibleFunctions_.end()) {
                throw Diagnostic("CALL_ERROR", expression.span.begin,
                                 "function '" + expression.name +
                                     "' was not declared before this call");
            }
            if (expression.arguments.size() > 6 ||
                expression.arguments.size() != function->second.parameterCount) {
                std::ostringstream message;
                message << "function '" << expression.name << "' expects "
                        << function->second.parameterCount << " arguments, got "
                        << expression.arguments.size();
                throw Diagnostic("CALL_ERROR", expression.span.begin, message.str());
            }
            for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
                requireInt(*expression.arguments[i], scope);
            }
            expression.functionId = function->second.symbolId;
            expression.isVoid = function->second.returnsVoid;
            addResolution(expression.name, "function", expression.functionId,
                          expression.span.begin);
            if (expression.isVoid && !allowVoidResult) {
                throw Diagnostic("TYPE_ERROR", expression.span.begin,
                                 "void function call cannot be used as a value");
            }
            return;
        }
        case ExprKind::Unary:
            requireInt(*expression.right, scope);
            return;
        case ExprKind::Binary:
            requireInt(*expression.left, scope);
            requireInt(*expression.right, scope);
            return;
        case ExprKind::Assignment: {
            if (!expression.left || expression.left->kind != ExprKind::Variable) {
                throw Diagnostic("TYPE_ERROR", expression.span.begin,
                                 "assignment target must be an identifier");
            }
            LocalInfo const* local = findLocal(scope, expression.left->name);
            if (local == NULL) {
                if (visibleFunctions_.find(expression.left->name) !=
                    visibleFunctions_.end()) {
                    throw Diagnostic("TYPE_ERROR", expression.left->span.begin,
                                     "cannot assign to function '" +
                                         expression.left->name + "'");
                }
                throw Diagnostic("NAME_ERROR", expression.left->span.begin,
                                 "assignment to undeclared variable '" +
                                     expression.left->name + "'");
            }
            expression.left->localId = local->id;
            addResolution(expression.left->name, "assignment-target", local->id,
                          expression.left->span.begin);
            requireInt(*expression.right, scope);
            return;
        }
        }
    }

    void analyzeStatement(Statement& statement, Scope& parent,
                          unsigned loopDepth, bool reuseBlockScope,
                          bool functionReturnsVoid) {
        switch (statement.kind) {
        case StmtKind::Block: {
            if (reuseBlockScope) {
                for (std::size_t i = 0; i < statement.statements.size(); ++i) {
                    analyzeStatement(*statement.statements[i], parent, loopDepth,
                                     false, functionReturnsVoid);
                }
                break;
            }
            Scope nested;
            nested.parent = &parent;
            nested.id = nextScopeId_++;
            nested.depth = parent.depth + 1;
            nested.declarations = 0;
            for (std::size_t i = 0; i < statement.statements.size(); ++i) {
                analyzeStatement(*statement.statements[i], nested, loopDepth, false,
                                 functionReturnsVoid);
            }
            break;
        }
        case StmtKind::Declaration:
            declareLocal(parent, statement.name, statement.span.begin, "local",
                         statement.localId);
            if (statement.first) requireInt(*statement.first, parent);
            break;
        case StmtKind::Expression:
            if (statement.first) analyzeExpression(*statement.first, parent, true);
            break;
        case StmtKind::If:
            requireInt(*statement.first, parent);
            analyzeStatement(*statement.statements[0], parent, loopDepth, false,
                             functionReturnsVoid);
            if (statement.statements.size() > 1) {
                analyzeStatement(*statement.statements[1], parent, loopDepth, false,
                                 functionReturnsVoid);
            }
            break;
        case StmtKind::While:
            requireInt(*statement.first, parent);
            analyzeStatement(*statement.statements[0], parent, loopDepth + 1, false,
                             functionReturnsVoid);
            break;
        case StmtKind::For: {
            Scope loopScope;
            loopScope.parent = &parent;
            loopScope.id = nextScopeId_++;
            loopScope.depth = parent.depth + 1;
            loopScope.declarations = 0;
            if (!statement.name.empty()) {
                declareLocal(loopScope, statement.name, statement.span.begin, "local",
                             statement.localId);
                if (statement.first) requireInt(*statement.first, loopScope);
            } else if (statement.first) {
                analyzeExpression(*statement.first, loopScope, true);
            }
            if (statement.second) requireInt(*statement.second, loopScope);
            if (statement.third) analyzeExpression(*statement.third, loopScope, true);
            analyzeStatement(*statement.statements[0], loopScope, loopDepth + 1,
                             false, functionReturnsVoid);
            break;
        }
        case StmtKind::Break:
        case StmtKind::Continue:
            if (loopDepth == 0) {
                throw Diagnostic("CONTROL_FLOW_ERROR", statement.span.begin,
                                 statement.kind == StmtKind::Break
                                     ? "break is only valid inside a loop"
                                     : "continue is only valid inside a loop");
            }
            break;
        case StmtKind::Return:
            if (functionReturnsVoid) {
                if (statement.first) {
                    throw Diagnostic("RETURN_ERROR", statement.span.begin,
                                     "void function cannot return a value");
                }
            } else {
                if (!statement.first) {
                    throw Diagnostic("RETURN_ERROR", statement.span.begin,
                                     "int function must return an expression");
                }
                requireInt(*statement.first, parent);
            }
            break;
        case StmtKind::Empty:
            break;
        }
    }

    SemanticResult result_;
    std::map<std::string, FunctionInfo> functions_;
    std::map<std::string, FunctionInfo> visibleFunctions_;
    int nextSymbolId_ = 1;
    int nextScopeId_ = 1;
};

std::string locationText(const SourceLocation& location) {
    std::ostringstream out;
    out << location.line << ':' << location.column;
    return out.str();
}

} // namespace

SemanticResult analyze(Program& program) {
    return Analyzer().run(program);
}

std::string dumpSymbols(const SemanticResult& result) {
    std::ostringstream out;
    out << "Symbols\n";
    for (std::size_t i = 0; i < result.symbols.size(); ++i) {
        const SymbolRecord& symbol = result.symbols[i];
        out << "  #" << symbol.id << ' ' << symbol.kind << ' ' << symbol.name;
        if (!symbol.signature.empty()) out << " : " << symbol.signature;
        if (symbol.scopeId != 0) {
            out << " scope#" << symbol.scopeId << " depth=" << symbol.depth;
        }
        out << " @" << locationText(symbol.location) << '\n';
    }
    out << "Resolutions\n";
    for (std::size_t i = 0; i < result.resolutions.size(); ++i) {
        const ResolutionRecord& resolution = result.resolutions[i];
        out << "  " << resolution.kind << ' ' << resolution.name << " -> #"
            << resolution.targetId << " @" << locationText(resolution.location)
            << '\n';
    }
    return out.str();
}

} // namespace cminor
