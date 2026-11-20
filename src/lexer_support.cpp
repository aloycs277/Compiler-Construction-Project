#include "cminor/frontend.hpp"

#include <sstream>

namespace cminor {

Diagnostic::Diagnostic(const std::string& diagnosticCode,
                       const SourceLocation& sourceLocation,
                       const std::string& message)
    : std::runtime_error(message), code(diagnosticCode), location(sourceLocation) {}

const char* tokenName(TokenKind kind) {
    switch (kind) {
    case TokenKind::End: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::Integer: return "integer";
    case TokenKind::String: return "string";
    case TokenKind::Character: return "character";
    case TokenKind::KwInt: return "int";
    case TokenKind::KwVoid: return "void";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwWhile: return "while";
    case TokenKind::KwFor: return "for";
    case TokenKind::KwBreak: return "break";
    case TokenKind::KwContinue: return "continue";
    case TokenKind::KwReturn: return "return";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Comma: return ",";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Less: return "<";
    case TokenKind::LessEqual: return "<=";
    case TokenKind::Greater: return ">";
    case TokenKind::GreaterEqual: return ">=";
    case TokenKind::EqualEqual: return "==";
    case TokenKind::NotEqual: return "!=";
    case TokenKind::Bang: return "!";
    case TokenKind::AndAnd: return "&&";
    case TokenKind::OrOr: return "||";
    case TokenKind::Assign: return "=";
    }
    return "unknown token";
}

std::string dumpTokens(const std::vector<Token>& tokens) {
    std::ostringstream out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        out << token.span.begin.line << ':' << token.span.begin.column << ' '
            << tokenName(token.kind);
        if (token.kind != TokenKind::End) out << " \"" << token.text << '"';
        out << '\n';
    }
    return out.str();
}

} // namespace cminor
