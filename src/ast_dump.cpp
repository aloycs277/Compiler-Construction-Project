#include "cminor/frontend.hpp"

#include <sstream>

namespace cminor {
namespace {

std::string locationText(const SourceLocation& location) {
    std::ostringstream out;
    out << location.line << ':' << location.column;
    return out.str();
}

void indent(std::ostringstream& out, unsigned depth) {
    while (depth-- != 0) out << "  ";
}

void dumpExpression(std::ostringstream& out, const Expr& expression,
                    unsigned depth) {
    indent(out, depth);
    out << "Expr ";
    switch (expression.kind) {
    case ExprKind::Integer:
        out << "Integer " << expression.integer;
        break;
    case ExprKind::String:
        out << "String";
        break;
    case ExprKind::Variable:
        out << "Variable " << expression.name;
        break;
    case ExprKind::Call:
        out << "Call " << expression.name;
        break;
    case ExprKind::Unary:
        out << "Unary " << tokenName(expression.op);
        break;
    case ExprKind::Binary:
        out << "Binary " << tokenName(expression.op);
        break;
    case ExprKind::Assignment:
        out << "Assignment";
        break;
    }
    out << " @" << locationText(expression.span.begin) << '-'
        << locationText(expression.span.end) << '\n';

    if (expression.left) dumpExpression(out, *expression.left, depth + 1);
    if (expression.right) dumpExpression(out, *expression.right, depth + 1);
    for (std::size_t i = 0; i < expression.arguments.size(); ++i) {
        dumpExpression(out, *expression.arguments[i], depth + 1);
    }
}

void dumpStatement(std::ostringstream& out, const Statement& statement,
                   unsigned depth) {
    indent(out, depth);
    switch (statement.kind) {
    case StmtKind::Block: out << "Block"; break;
    case StmtKind::Declaration: out << "Declaration " << statement.name; break;
    case StmtKind::Expression: out << "ExpressionStatement"; break;
    case StmtKind::If: out << "If"; break;
    case StmtKind::While: out << "While"; break;
    case StmtKind::For:
        out << "For";
        if (!statement.name.empty()) out << " declaration " << statement.name;
        break;
    case StmtKind::Break: out << "Break"; break;
    case StmtKind::Continue: out << "Continue"; break;
    case StmtKind::Return: out << "Return"; break;
    case StmtKind::Empty: out << "EmptyStatement"; break;
    }
    out << " @" << locationText(statement.span.begin) << '-'
        << locationText(statement.span.end) << '\n';
    if (statement.first) dumpExpression(out, *statement.first, depth + 1);
    if (statement.second) dumpExpression(out, *statement.second, depth + 1);
    if (statement.third) dumpExpression(out, *statement.third, depth + 1);
    for (std::size_t i = 0; i < statement.statements.size(); ++i) {
        dumpStatement(out, *statement.statements[i], depth + 1);
    }
}

} // namespace

std::string dumpAst(const Program& program) {
    std::ostringstream out;
    out << "Program\n";
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const Function& function = program.functions[i];
        out << "  Function " << (function.returnsVoid ? "void " : "int ")
            << function.name << '(';
        for (std::size_t p = 0; p < function.parameters.size(); ++p) {
            if (p != 0) out << ", ";
            out << "int";
            if (!function.parameters[p].name.empty()) {
                out << ' ' << function.parameters[p].name;
            }
        }
        if (function.parameters.empty()) out << "void";
        out << ") " << (function.definition ? "definition" : "prototype")
            << " @" << locationText(function.span.begin) << '-'
            << locationText(function.span.end) << '\n';
        if (function.body) dumpStatement(out, *function.body, 2);
    }
    return out.str();
}

} // namespace cminor
