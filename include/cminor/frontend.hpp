#ifndef CMINOR_FRONTEND_HPP
#define CMINOR_FRONTEND_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cminor {

struct SourceLocation {
    std::string path;
    int line;
    int column;
};

struct SourceSpan {
    SourceLocation begin;
    SourceLocation end;
};

struct Diagnostic : std::runtime_error {
    std::string code;
    SourceLocation location;

    Diagnostic(const std::string& code, const SourceLocation& location,
               const std::string& message);
};

enum class TokenKind {
    End, Identifier, Integer, String, Character,
    KwInt, KwVoid, KwIf, KwElse, KwWhile, KwFor, KwBreak, KwContinue, KwReturn,
    LParen, RParen, LBrace, RBrace, Semicolon, Comma,
    Plus, Minus, Star, Slash, Percent, Less, LessEqual, Greater, GreaterEqual,
    EqualEqual, NotEqual, Bang, AndAnd, OrOr, Assign
};

struct Token {
    TokenKind kind;
    std::string text;
    std::string value;
    std::int32_t integer;
    SourceSpan span;
};

const char* tokenName(TokenKind kind);
std::vector<Token> lex(const std::string& source, const std::string& path);

enum class ExprKind { Integer, String, Variable, Call, Unary, Binary, Assignment };

struct Expr {
    ExprKind kind;
    SourceSpan span;
    TokenKind op;
    std::string name;
    std::string value;
    std::int32_t integer;
    int localId;
    int functionId;
    bool isVoid;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::vector<std::unique_ptr<Expr> > arguments;

    explicit Expr(ExprKind kind, const SourceSpan& span);
};

enum class StmtKind {
    Block, Declaration, Expression, If, While, For, Break, Continue, Return, Empty
};

struct Statement {
    StmtKind kind;
    SourceSpan span;
    std::string name;
    int localId;
    std::unique_ptr<Expr> first;
    std::unique_ptr<Expr> second;
    std::unique_ptr<Expr> third;
    std::vector<std::unique_ptr<Statement> > statements;

    explicit Statement(StmtKind kind, const SourceSpan& span);
};

struct Parameter {
    std::string name;
    SourceLocation location;
    int localId;
};

struct Function {
    std::string name;
    bool returnsVoid;
    bool definition;
    bool explicitVoidParams;
    SourceSpan span;
    int symbolId;
    std::vector<Parameter> parameters;
    std::unique_ptr<Statement> body;
};

struct Program {
    std::vector<Function> functions;
};

struct SymbolRecord {
    int id;
    std::string name;
    std::string kind;
    std::string signature;
    int scopeId;
    int depth;
    SourceLocation location;
};

struct ResolutionRecord {
    std::string name;
    std::string kind;
    int targetId;
    SourceLocation location;
};

struct SemanticResult {
    std::vector<SymbolRecord> symbols;
    std::vector<ResolutionRecord> resolutions;
};

SemanticResult analyze(Program& program);
std::string dumpSymbols(const SemanticResult& result);

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens);
    Program parse();

private:
    const Token& current() const;
    const Token& previous() const;
    bool match(TokenKind kind);
    const Token& consume(TokenKind kind, const std::string& expectation);
    [[noreturn]] void fail(const std::string& message) const;

    Function parseFunction();
    std::vector<Parameter> parseParameters(bool prototype);
    std::unique_ptr<Statement> parseBlock();
    std::unique_ptr<Statement> parseBlockItem();
    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<Statement> parseDeclaration(bool consumeSemicolon);
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseAssignment();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseRelational();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePrimary();
    SourceSpan joined(const SourceSpan& first, const SourceSpan& last) const;

    const std::vector<Token>& tokens_;
    std::size_t position_;
    unsigned expressionDepth_;
    unsigned blockDepth_;
    unsigned unaryDepth_;
    unsigned statementDepth_;
};

std::string dumpAst(const Program& program);
std::string dumpTokens(const std::vector<Token>& tokens);

} // namespace cminor

#endif
