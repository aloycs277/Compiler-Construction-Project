#include "cminor/frontend.hpp"

#include <sstream>

namespace cminor {

Expr::Expr(ExprKind expressionKind, const SourceSpan& sourceSpan)
    : kind(expressionKind), span(sourceSpan), op(TokenKind::End), integer(0),
      localId(-1), functionId(-1), isVoid(false) {}

Statement::Statement(StmtKind statementKind, const SourceSpan& sourceSpan)
    : kind(statementKind), span(sourceSpan), localId(-1) {}

Parser::Parser(const std::vector<Token>& tokens)
    : tokens_(tokens), position_(0), expressionDepth_(0), blockDepth_(0),
      unaryDepth_(0), statementDepth_(0) {}

const Token& Parser::current() const {
    return tokens_[position_ < tokens_.size() ? position_ : tokens_.size() - 1];
}

const Token& Parser::previous() const {
    return tokens_[position_ == 0 ? 0 : position_ - 1];
}

bool Parser::match(TokenKind kind) {
    if (current().kind != kind) return false;
    ++position_;
    return true;
}

const Token& Parser::consume(TokenKind kind, const std::string& expectation) {
    if (current().kind != kind) fail("expected " + expectation + ", found " +
                                     tokenName(current().kind));
    return tokens_[position_++];
}

[[noreturn]] void Parser::fail(const std::string& message) const {
    throw Diagnostic("SYNTAX_ERROR", current().span.begin, message);
}

SourceSpan Parser::joined(const SourceSpan& first, const SourceSpan& last) const {
    SourceSpan span = first;
    span.end = last.end;
    return span;
}

Program Parser::parse() {
    Program program;
    while (current().kind != TokenKind::End) {
        const std::size_t oldPosition = position_;
        program.functions.push_back(parseFunction());
        if (position_ == oldPosition) fail("parser made no progress");
    }
    if (program.functions.empty()) {
        fail("a compilation unit must contain at least one function");
    }
    return program;
}

Function Parser::parseFunction() {
    const Token& returnType = current();
    if (!match(TokenKind::KwInt) && !match(TokenKind::KwVoid)) {
        fail("expected function return type 'int' or 'void'");
    }
    Function function;
    function.returnsVoid = returnType.kind == TokenKind::KwVoid;
    function.definition = false;
    function.explicitVoidParams = false;
    function.symbolId = -1;
    function.span.begin = returnType.span.begin;
    function.span.end = returnType.span.end;
    if (current().kind == TokenKind::Star) {
        throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                         "pointer declarations are outside C Minor");
    }
    const Token& name = consume(TokenKind::Identifier, "function name");
    function.name = name.text;
    function.span.end = name.span.end;
    if (current().kind == TokenKind::Semicolon ||
        current().kind == TokenKind::Assign) {
        throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                         "global objects are outside C Minor");
    }
    consume(TokenKind::LParen, "'('");
    function.explicitVoidParams = current().kind == TokenKind::KwVoid;
    function.parameters = parseParameters(false);
    const Token& closingParen = consume(TokenKind::RParen, "')'");
    function.span.end = closingParen.span.end;
    if (match(TokenKind::Semicolon)) {
        function.span.end = previous().span.end;
        return function;
    }
    function.definition = true;
    for (std::size_t i = 0; i < function.parameters.size(); ++i) {
        if (function.parameters[i].name.empty()) {
            throw Diagnostic("SYNTAX_ERROR", function.parameters[i].location,
                             "function definitions require a name for each parameter");
        }
    }
    function.body = parseBlock();
    function.span.end = function.body->span.end;
    return function;
}

std::vector<Parameter> Parser::parseParameters(bool definition) {
    std::vector<Parameter> parameters;
    if (match(TokenKind::KwVoid)) {
        if (current().kind != TokenKind::RParen) {
            fail("'void' may only mark an empty parameter list");
        }
        return parameters;
    }
    if (current().kind == TokenKind::RParen) {
        fail("zero-parameter function declarations must use '(void)'");
    }

    do {
        consume(TokenKind::KwInt, "'int' in parameter declaration");
        if (current().kind == TokenKind::Star) {
            throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                             "pointer parameters are outside C Minor");
        }
        Parameter parameter;
        parameter.location = current().span.begin;
        parameter.localId = -1;
        if (current().kind == TokenKind::Identifier) {
            const Token& identifier = tokens_[position_++];
            parameter.name = identifier.text;
            parameter.location = identifier.span.begin;
        } else if (definition) {
            fail("function definitions require a name for each parameter");
        }
        parameters.push_back(parameter);
        if (parameters.size() > 6) {
            throw Diagnostic("UNSUPPORTED_FEATURE", parameter.location,
                             "functions may have at most six parameters");
        }
    } while (match(TokenKind::Comma));
    return parameters;
}

std::unique_ptr<Statement> Parser::parseBlock() {
    const Token& opening = consume(TokenKind::LBrace, "'{'");
    if (++blockDepth_ > 256) {
        --blockDepth_;
        throw Diagnostic("LIMIT_ERROR", opening.span.begin,
                         "block nesting exceeds the implementation limit of 256");
    }
    std::unique_ptr<Statement> block(new Statement(StmtKind::Block, opening.span));
    while (current().kind != TokenKind::RBrace &&
           current().kind != TokenKind::End) {
        const std::size_t oldPosition = position_;
        block->statements.push_back(parseBlockItem());
        if (position_ == oldPosition) fail("parser made no progress in block");
    }
    const Token& closing = consume(TokenKind::RBrace, "'}'");
    block->span.end = closing.span.end;
    --blockDepth_;
    return block;
}

std::unique_ptr<Statement> Parser::parseBlockItem() {
    if (current().kind == TokenKind::KwInt) return parseDeclaration(true);
    if (current().kind == TokenKind::KwVoid) {
        throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                         "void objects are outside C Minor");
    }
    return parseStatement();
}

std::unique_ptr<Statement> Parser::parseDeclaration(bool consumeSemicolon) {
    const Token& type = consume(TokenKind::KwInt, "'int'");
    if (current().kind == TokenKind::Star) {
        throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                         "pointer declarations are outside C Minor");
    }
    const Token& name = consume(TokenKind::Identifier, "local variable name");
    std::unique_ptr<Statement> declaration(
        new Statement(StmtKind::Declaration, joined(type.span, name.span)));
    declaration->name = name.text;
    if (match(TokenKind::Assign)) declaration->first = parseAssignment();
    if (consumeSemicolon) {
        const Token& semicolon = consume(TokenKind::Semicolon, "';'");
        declaration->span.end = semicolon.span.end;
    } else if (declaration->first) {
        declaration->span.end = declaration->first->span.end;
    }
    return declaration;
}

std::unique_ptr<Statement> Parser::parseStatement() {
    if (++statementDepth_ > 256) {
        --statementDepth_;
        throw Diagnostic("LIMIT_ERROR", current().span.begin,
                         "statement nesting exceeds the implementation limit of 256");
    }
    struct DepthGuard {
        unsigned& depth;
        explicit DepthGuard(unsigned& value) : depth(value) {}
        ~DepthGuard() { --depth; }
    } depthGuard(statementDepth_);

    if (current().kind == TokenKind::LBrace) return parseBlock();

    const Token start = current();
    if (match(TokenKind::Semicolon)) {
        return std::unique_ptr<Statement>(
            new Statement(StmtKind::Empty, start.span));
    }
    if (match(TokenKind::KwIf)) {
        std::unique_ptr<Statement> statement(new Statement(StmtKind::If, start.span));
        consume(TokenKind::LParen, "'(' after if");
        statement->first = parseAssignment();
        consume(TokenKind::RParen, "')' after if condition");
        statement->statements.push_back(parseStatement());
        if (match(TokenKind::KwElse)) statement->statements.push_back(parseStatement());
        statement->span.end = statement->statements.back()->span.end;
        return statement;
    }
    if (match(TokenKind::KwWhile)) {
        std::unique_ptr<Statement> statement(
            new Statement(StmtKind::While, start.span));
        consume(TokenKind::LParen, "'(' after while");
        statement->first = parseAssignment();
        consume(TokenKind::RParen, "')' after while condition");
        statement->statements.push_back(parseStatement());
        statement->span.end = statement->statements.back()->span.end;
        return statement;
    }
    if (match(TokenKind::KwFor)) {
        std::unique_ptr<Statement> statement(
            new Statement(StmtKind::For, start.span));
        consume(TokenKind::LParen, "'(' after for");
        if (current().kind == TokenKind::KwInt) {
            std::unique_ptr<Statement> declaration = parseDeclaration(false);
            statement->name = declaration->name;
            statement->first = std::move(declaration->first);
        } else if (current().kind != TokenKind::Semicolon) {
            statement->first = parseAssignment();
        }
        consume(TokenKind::Semicolon, "';' after for initializer");
        if (current().kind != TokenKind::Semicolon) {
            statement->second = parseAssignment();
        }
        consume(TokenKind::Semicolon, "';' after for condition");
        if (current().kind != TokenKind::RParen) {
            statement->third = parseAssignment();
        }
        consume(TokenKind::RParen, "')' after for clauses");
        statement->statements.push_back(parseStatement());
        statement->span.end = statement->statements.back()->span.end;
        return statement;
    }
    if (match(TokenKind::KwBreak)) {
        const Token& semicolon = consume(TokenKind::Semicolon, "';' after break");
        std::unique_ptr<Statement> statement(
            new Statement(StmtKind::Break, joined(start.span, semicolon.span)));
        return statement;
    }
    if (match(TokenKind::KwContinue)) {
        const Token& semicolon = consume(TokenKind::Semicolon, "';' after continue");
        return std::unique_ptr<Statement>(
            new Statement(StmtKind::Continue, joined(start.span, semicolon.span)));
    }
    if (match(TokenKind::KwReturn)) {
        std::unique_ptr<Statement> statement(
            new Statement(StmtKind::Return, start.span));
        if (current().kind != TokenKind::Semicolon) {
            statement->first = parseAssignment();
        }
        const Token& semicolon = consume(TokenKind::Semicolon, "';' after return");
        statement->span.end = semicolon.span.end;
        return statement;
    }

    std::unique_ptr<Statement> statement(
        new Statement(StmtKind::Expression, start.span));
    if (current().kind != TokenKind::Semicolon) {
        statement->first = parseAssignment();
    }
    const Token& semicolon = consume(TokenKind::Semicolon, "';' after expression");
    statement->span.end = semicolon.span.end;
    return statement;
}

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> Parser::parseAssignment() {
    if (++expressionDepth_ > 256) {
        --expressionDepth_;
        throw Diagnostic("LIMIT_ERROR", current().span.begin,
                         "expression nesting exceeds the implementation limit of 256");
    }
    std::unique_ptr<Expr> left = parseLogicalOr();
    if (match(TokenKind::Assign)) {
        const Token& op = previous();
        std::unique_ptr<Expr> right = parseAssignment();
        std::unique_ptr<Expr> result(new Expr(ExprKind::Assignment,
                                               joined(left->span, right->span)));
        result->op = op.kind;
        result->left = std::move(left);
        result->right = std::move(right);
        --expressionDepth_;
        return result;
    }
    --expressionDepth_;
    return left;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    std::unique_ptr<Expr> expression = parseLogicalAnd();
    while (match(TokenKind::OrOr)) {
        const Token& op = previous();
        std::unique_ptr<Expr> right = parseLogicalAnd();
        std::unique_ptr<Expr> result(
            new Expr(ExprKind::Binary, joined(expression->span, right->span)));
        result->op = op.kind;
        result->left = std::move(expression);
        result->right = std::move(right);
        expression = std::move(result);
    }
    return expression;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    std::unique_ptr<Expr> expression = parseEquality();
    while (match(TokenKind::AndAnd)) {
        const Token& op = previous();
        std::unique_ptr<Expr> right = parseEquality();
        std::unique_ptr<Expr> result(
            new Expr(ExprKind::Binary, joined(expression->span, right->span)));
        result->op = op.kind;
        result->left = std::move(expression);
        result->right = std::move(right);
        expression = std::move(result);
    }
    return expression;
}

std::unique_ptr<Expr> Parser::parseEquality() {
    std::unique_ptr<Expr> expression = parseRelational();
    while (current().kind == TokenKind::EqualEqual ||
           current().kind == TokenKind::NotEqual) {
        const Token& op = tokens_[position_++];
        std::unique_ptr<Expr> right = parseRelational();
        std::unique_ptr<Expr> result(
            new Expr(ExprKind::Binary, joined(expression->span, right->span)));
        result->op = op.kind;
        result->left = std::move(expression);
        result->right = std::move(right);
        expression = std::move(result);
    }
    return expression;
}

std::unique_ptr<Expr> Parser::parseRelational() {
    std::unique_ptr<Expr> expression = parseAdditive();
    while (current().kind == TokenKind::Less ||
           current().kind == TokenKind::LessEqual ||
           current().kind == TokenKind::Greater ||
           current().kind == TokenKind::GreaterEqual) {
        const Token& op = tokens_[position_++];
        std::unique_ptr<Expr> right = parseAdditive();
        std::unique_ptr<Expr> result(
            new Expr(ExprKind::Binary, joined(expression->span, right->span)));
        result->op = op.kind;
        result->left = std::move(expression);
        result->right = std::move(right);
        expression = std::move(result);
    }
    return expression;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    std::unique_ptr<Expr> expression = parseMultiplicative();
    while (current().kind == TokenKind::Plus || current().kind == TokenKind::Minus) {
        const Token& op = tokens_[position_++];
        std::unique_ptr<Expr> right = parseMultiplicative();
        std::unique_ptr<Expr> result(
            new Expr(ExprKind::Binary, joined(expression->span, right->span)));
        result->op = op.kind;
        result->left = std::move(expression);
        result->right = std::move(right);
        expression = std::move(result);
    }
    return expression;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    std::unique_ptr<Expr> expression = parseUnary();
    while (current().kind == TokenKind::Star ||
           current().kind == TokenKind::Slash ||
           current().kind == TokenKind::Percent) {
        const Token& op = tokens_[position_++];
        std::unique_ptr<Expr> right = parseUnary();
        std::unique_ptr<Expr> result(
            new Expr(ExprKind::Binary, joined(expression->span, right->span)));
        result->op = op.kind;
        result->left = std::move(expression);
        result->right = std::move(right);
        expression = std::move(result);
    }
    return expression;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (++unaryDepth_ > 256) {
        --unaryDepth_;
        throw Diagnostic("LIMIT_ERROR", current().span.begin,
                         "unary expression nesting exceeds the implementation limit of 256");
    }
    struct DepthGuard {
        unsigned& depth;
        explicit DepthGuard(unsigned& value) : depth(value) {}
        ~DepthGuard() { --depth; }
    } depthGuard(unaryDepth_);

    if (current().kind == TokenKind::Star) {
        throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                         "pointer dereference is outside C Minor");
    }
    if (current().kind == TokenKind::Plus ||
        current().kind == TokenKind::Minus ||
        current().kind == TokenKind::Bang) {
        const Token& op = tokens_[position_++];
        std::unique_ptr<Expr> operand = parseUnary();
        std::unique_ptr<Expr> expression(
            new Expr(ExprKind::Unary, joined(op.span, operand->span)));
        expression->op = op.kind;
        expression->right = std::move(operand);
        return expression;
    }
    return parsePrimary();
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    const Token token = current();
    if (match(TokenKind::Integer)) {
        std::unique_ptr<Expr> expression(
            new Expr(ExprKind::Integer, token.span));
        expression->integer = token.integer;
        return expression;
    }
    if (match(TokenKind::Character)) {
        std::unique_ptr<Expr> expression(
            new Expr(ExprKind::Integer, token.span));
        expression->integer = token.integer;
        return expression;
    }
    if (match(TokenKind::String)) {
        std::unique_ptr<Expr> expression(new Expr(ExprKind::String, token.span));
        expression->value = token.value;
        return expression;
    }
    if (match(TokenKind::Identifier)) {
        std::unique_ptr<Expr> expression(
            new Expr(ExprKind::Variable, token.span));
        expression->name = token.text;
        if (!match(TokenKind::LParen)) return expression;
        expression->kind = ExprKind::Call;
        if (current().kind != TokenKind::RParen) {
            do {
                expression->arguments.push_back(parseAssignment());
            } while (match(TokenKind::Comma));
        }
        const Token& closing = consume(TokenKind::RParen, "')' after arguments");
        expression->span.end = closing.span.end;
        return expression;
    }
    if (match(TokenKind::LParen)) {
        if (current().kind == TokenKind::KwInt ||
            current().kind == TokenKind::KwVoid) {
            throw Diagnostic("UNSUPPORTED_FEATURE", current().span.begin,
                             "type casts are outside C Minor");
        }
        std::unique_ptr<Expr> expression = parseAssignment();
        const Token& closing = consume(TokenKind::RParen, "')' after expression");
        expression->span = joined(token.span, closing.span);
        return expression;
    }
    fail("expected expression, found " + std::string(tokenName(current().kind)));
}

} // namespace cminor
