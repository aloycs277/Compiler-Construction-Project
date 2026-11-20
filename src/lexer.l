%option noyywrap nodefault 8bit noinput nounput

%{
#include "cminor/frontend.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <set>
#include <sstream>

namespace cminor {
namespace {

std::string scannerPath;
std::vector<Token>* scannerTokens = NULL;
int scannerLine = 1;
int scannerColumn = 1;
bool scannerLineHasToken = false;
SourceLocation scannerMatchStart;
SourceLocation scannerCommentStart;

SourceLocation scannerLocation() {
    SourceLocation location;
    location.path = scannerPath;
    location.line = scannerLine;
    location.column = scannerColumn;
    return location;
}

void advanceScannerLocation(const char* text, int length) {
    scannerMatchStart = scannerLocation();
    for (int i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            ++scannerLine;
            scannerColumn = 1;
            scannerLineHasToken = false;
        } else {
            ++scannerColumn;
        }
    }
}

Token makeToken(TokenKind kind, const char* text, int length,
                std::int32_t integer = 0) {
    Token token;
    token.kind = kind;
    token.text.assign(text, static_cast<std::size_t>(length));
    token.integer = integer;
    token.span.begin = scannerMatchStart;
    token.span.end = scannerLocation();
    scannerLineHasToken = true;
    return token;
}

void appendToken(TokenKind kind, const char* text, int length,
                 std::int32_t integer = 0) {
    scannerTokens->push_back(makeToken(kind, text, length, integer));
}

void lexicalError(const char* code, const SourceLocation& location,
                  const std::string& message) {
    throw Diagnostic(code, location, message);
}

TokenKind identifierKind(const std::string& word) {
    if (word == "int") return TokenKind::KwInt;
    if (word == "void") return TokenKind::KwVoid;
    if (word == "if") return TokenKind::KwIf;
    if (word == "else") return TokenKind::KwElse;
    if (word == "while") return TokenKind::KwWhile;
    if (word == "for") return TokenKind::KwFor;
    if (word == "break") return TokenKind::KwBreak;
    if (word == "continue") return TokenKind::KwContinue;
    if (word == "return") return TokenKind::KwReturn;

    static const std::set<std::string> reserved = {
        "alignas", "alignof", "auto", "bool", "case", "char", "const",
        "constexpr", "default", "do", "double", "enum", "extern", "false",
        "float", "goto", "inline", "long", "nullptr", "register", "restrict",
        "short", "signed", "sizeof", "static", "static_assert", "struct",
        "switch", "thread_local", "true", "typedef", "typeof",
        "typeof_unqual", "union", "unsigned", "volatile", "_Alignas",
        "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128",
        "_Decimal32", "_Decimal64", "_Generic", "_Imaginary", "_Noreturn",
        "_Static_assert", "_Thread_local"
    };
    if (reserved.find(word) != reserved.end()) {
        lexicalError("UNSUPPORTED_FEATURE", scannerMatchStart,
                     "reserved word '" + word + "' is outside C Minor");
    }
    return TokenKind::Identifier;
}

void appendIdentifier(const char* text, int length) {
    const std::string word(text, static_cast<std::size_t>(length));
    appendToken(identifierKind(word), text, length);
}

void appendInteger(const char* text, int length) {
    const std::string literal(text, static_cast<std::size_t>(length));
    const bool validForm = literal == "0" ||
        (!literal.empty() && literal[0] >= '1' && literal[0] <= '9' &&
         literal.find_first_not_of("0123456789") == std::string::npos);
    if (!validForm) {
        lexicalError("UNSUPPORTED_FEATURE", scannerMatchStart,
                     "malformed or unsupported integer literal '" + literal + "'");
    }

    errno = 0;
    char* end = NULL;
    const long long value = std::strtoll(literal.c_str(), &end, 10);
    if (errno == ERANGE || end == literal.c_str() || *end != '\0' ||
        value > 2147483647LL) {
        lexicalError("UNSUPPORTED_FEATURE", scannerMatchStart,
                     "integer literal is outside the supported range 0..2147483647");
    }
    appendToken(TokenKind::Integer, text, length,
                static_cast<std::int32_t>(value));
}

std::string decodeLiteral(const char* text, int length) {
    std::string value;
    for (int i = 1; i < length - 1; ++i) {
        unsigned char character = static_cast<unsigned char>(text[i]);
        if (character != '\\') {
            value += static_cast<char>(character);
            continue;
        }
        if (++i >= length - 1) {
            lexicalError("LEXICAL_ERROR", scannerMatchStart,
                         "incomplete escape sequence in literal");
        }
        switch (text[i]) {
        case 'a': value += '\a'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'n': value += '\n'; break;
        case 'r': value += '\r'; break;
        case 't': value += '\t'; break;
        case 'v': value += '\v'; break;
        case '\\': value += '\\'; break;
        case '\'': value += '\''; break;
        case '"': value += '"'; break;
        case '?': value += '?'; break;
        case 'x': {
            unsigned int byte = 0;
            bool hasDigit = false;
            while (i + 1 < length - 1) {
                const char digit = text[i + 1];
                unsigned int valueDigit;
                if (digit >= '0' && digit <= '9') valueDigit = digit - '0';
                else if (digit >= 'a' && digit <= 'f') valueDigit = digit - 'a' + 10;
                else if (digit >= 'A' && digit <= 'F') valueDigit = digit - 'A' + 10;
                else break;
                if (byte > 255U / 16U) {
                    lexicalError("LEXICAL_ERROR", scannerMatchStart,
                                 "hexadecimal escape is outside the byte range");
                }
                byte = byte * 16U + valueDigit;
                if (byte > 255U) {
                    lexicalError("LEXICAL_ERROR", scannerMatchStart,
                                 "hexadecimal escape is outside the byte range");
                }
                ++i;
                hasDigit = true;
            }
            if (!hasDigit) {
                lexicalError("LEXICAL_ERROR", scannerMatchStart,
                             "hexadecimal escape requires at least one digit");
            }
            value += static_cast<char>(byte);
            break;
        }
        default:
            if (text[i] >= '0' && text[i] <= '7') {
                unsigned int byte = static_cast<unsigned int>(text[i] - '0');
                int digits = 1;
                while (digits < 3 && i + 1 < length - 1 &&
                       text[i + 1] >= '0' && text[i + 1] <= '7') {
                    byte = byte * 8U + static_cast<unsigned int>(text[++i] - '0');
                    ++digits;
                }
                if (byte > 255U) {
                    lexicalError("LEXICAL_ERROR", scannerMatchStart,
                                 "octal escape is outside the byte range");
                }
                value += static_cast<char>(byte);
            } else {
                std::ostringstream message;
                message << "unsupported escape sequence '\\" << text[i]
                        << "' in literal";
                lexicalError("LEXICAL_ERROR", scannerMatchStart, message.str());
            }
        }
    }
    return value;
}

void appendString(const char* text, int length) {
    Token token = makeToken(TokenKind::String, text, length);
    token.value = decodeLiteral(text, length);
    scannerTokens->push_back(token);
}

void appendCharacter(const char* text, int length) {
    const std::string value = decodeLiteral(text, length);
    if (value.size() != 1) {
        lexicalError("LEXICAL_ERROR", scannerMatchStart,
                     "character literal must contain exactly one byte");
    }
    Token token = makeToken(TokenKind::Character, text, length,
                            static_cast<unsigned char>(value[0]));
    token.value = value;
    scannerTokens->push_back(token);
}

void appendOperator(TokenKind kind, const char* text, int length) {
    appendToken(kind, text, length);
}

void rejectUnsupportedOperator(const char* text, int length) {
    const std::string op(text, static_cast<std::size_t>(length));
    lexicalError("UNSUPPORTED_FEATURE", scannerMatchStart,
                 "operator '" + op + "' is not supported");
}

void rejectUnsupportedPunctuation(const char* text) {
    std::ostringstream message;
    message << "operator or punctuation '" << text[0]
            << "' is outside the C Minor language";
    lexicalError("UNSUPPORTED_FEATURE", scannerMatchStart, message.str());
}

} // namespace
} // namespace cminor

#define YY_USER_ACTION cminor::advanceScannerLocation(yytext, yyleng);
%}

ID          [_A-Za-z][_A-Za-z0-9]*
INTEGERLIKE [0-9][A-Za-z0-9_.]*
ESCAPE      \\([abfnrtv\\'"?]|[0-7]{1,3}|x[0-9A-Fa-f]+)
STRING      \"([^\"\\\r\n\x00\x80-\xff]|{ESCAPE})*\"
CHARACTER   \'([^\'\\\r\n\x00\x80-\xff]|{ESCAPE})\'

%x COMMENT

%%

[ \t\v\f]+                  ;
(\r\n|\n)                   ;
"//"[^\r\n\x80-\xff]*      ;
"/*"                        { cminor::scannerCommentStart = cminor::scannerMatchStart; BEGIN(COMMENT); }
<COMMENT>"*/"                { BEGIN(INITIAL); }
<COMMENT>\0                  { cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, "unexpected NUL byte"); }
<COMMENT>[\x80-\xff]         { cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, "non-ASCII source characters are not supported"); }
<COMMENT>(.|\r|\n)           ;
<COMMENT><<EOF>>             { cminor::lexicalError("LEXICAL_ERROR", cminor::scannerCommentStart, "unterminated block comment"); }

\\(\r\n|\n)                  { cminor::lexicalError("UNSUPPORTED_LINE_SPLICING", cminor::scannerMatchStart, "backslash-newline line splicing is not supported"); }
{STRING}                     { cminor::appendString(yytext, yyleng); }
{CHARACTER}                  { cminor::appendCharacter(yytext, yyleng); }
["']                         { cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, "malformed string or character literal"); }
"#"|"%:"                    {
                                if (!cminor::scannerLineHasToken) {
                                    cminor::lexicalError("UNSUPPORTED_PREPROCESSING_DIRECTIVE", cminor::scannerMatchStart, "preprocessing directives are not part of C Minor");
                                }
                                cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, "directive marker is only valid at the start of a source line");
                            }
[\x80-\xff]                  { cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, "non-ASCII source characters are not supported"); }
\0                           { cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, "unexpected NUL byte"); }

{ID}                         { cminor::appendIdentifier(yytext, yyleng); }
{INTEGERLIKE}                { cminor::appendInteger(yytext, yyleng); }

"++"|"--"|"+="|"-="|"*="|"/="|"%="|"<<"|">>"|"&="|"|="|"^="|"->" { cminor::rejectUnsupportedOperator(yytext, yyleng); }
"?"                          { cminor::lexicalError("UNSUPPORTED_FEATURE", cminor::scannerMatchStart, "conditional operator is not supported"); }
"&"                          { cminor::rejectUnsupportedPunctuation(yytext); }
"|"                          { cminor::rejectUnsupportedPunctuation(yytext); }
"^"                          { cminor::rejectUnsupportedPunctuation(yytext); }
"~"                          { cminor::rejectUnsupportedPunctuation(yytext); }
"["                          { cminor::rejectUnsupportedPunctuation(yytext); }
"]"                          { cminor::rejectUnsupportedPunctuation(yytext); }
"."                          { cminor::rejectUnsupportedPunctuation(yytext); }
":"                          { cminor::rejectUnsupportedPunctuation(yytext); }

"<="                         { cminor::appendOperator(cminor::TokenKind::LessEqual, yytext, yyleng); }
">="                         { cminor::appendOperator(cminor::TokenKind::GreaterEqual, yytext, yyleng); }
"=="                         { cminor::appendOperator(cminor::TokenKind::EqualEqual, yytext, yyleng); }
"!="                         { cminor::appendOperator(cminor::TokenKind::NotEqual, yytext, yyleng); }
"&&"                         { cminor::appendOperator(cminor::TokenKind::AndAnd, yytext, yyleng); }
"||"                         { cminor::appendOperator(cminor::TokenKind::OrOr, yytext, yyleng); }
"("                          { cminor::appendOperator(cminor::TokenKind::LParen, yytext, yyleng); }
")"                          { cminor::appendOperator(cminor::TokenKind::RParen, yytext, yyleng); }
"{"                          { cminor::appendOperator(cminor::TokenKind::LBrace, yytext, yyleng); }
"}"                          { cminor::appendOperator(cminor::TokenKind::RBrace, yytext, yyleng); }
";"                          { cminor::appendOperator(cminor::TokenKind::Semicolon, yytext, yyleng); }
","                          { cminor::appendOperator(cminor::TokenKind::Comma, yytext, yyleng); }
"+"                          { cminor::appendOperator(cminor::TokenKind::Plus, yytext, yyleng); }
"-"                          { cminor::appendOperator(cminor::TokenKind::Minus, yytext, yyleng); }
"*"                          { cminor::appendOperator(cminor::TokenKind::Star, yytext, yyleng); }
"/"                          { cminor::appendOperator(cminor::TokenKind::Slash, yytext, yyleng); }
"%"                          { cminor::appendOperator(cminor::TokenKind::Percent, yytext, yyleng); }
"<"                          { cminor::appendOperator(cminor::TokenKind::Less, yytext, yyleng); }
">"                          { cminor::appendOperator(cminor::TokenKind::Greater, yytext, yyleng); }
"!"                          { cminor::appendOperator(cminor::TokenKind::Bang, yytext, yyleng); }
"="                          { cminor::appendOperator(cminor::TokenKind::Assign, yytext, yyleng); }

.                            {
                                std::ostringstream message;
                                message << "unexpected character '" << yytext[0] << "'";
                                cminor::lexicalError("LEXICAL_ERROR", cminor::scannerMatchStart, message.str());
                            }

%%

namespace cminor {
namespace {

class ScannerBuffer {
public:
    explicit ScannerBuffer(YY_BUFFER_STATE buffer) : buffer_(buffer) {}
    ~ScannerBuffer() {
        yy_delete_buffer(buffer_);
        yylex_destroy();
    }

private:
    ScannerBuffer(const ScannerBuffer&);
    ScannerBuffer& operator=(const ScannerBuffer&);
    YY_BUFFER_STATE buffer_;
};

} // namespace

std::vector<Token> lex(const std::string& source, const std::string& path) {
    if (source.size() > static_cast<std::size_t>(INT_MAX)) {
        throw Diagnostic("LIMIT_ERROR", SourceLocation{path, 1, 1},
                         "source file is too large for the Flex scanner");
    }
    scannerPath = path;
    scannerTokens = new std::vector<Token>();
    scannerLine = 1;
    scannerColumn = 1;
    scannerLineHasToken = false;
    scannerCommentStart = scannerLocation();
    try {
        YY_BUFFER_STATE buffer =
            yy_scan_bytes(source.data(), static_cast<int>(source.size()));
        ScannerBuffer cleanup(buffer);
        while (yylex() != 0) {}
        const SourceLocation end = scannerLocation();
        Token eof;
        eof.kind = TokenKind::End;
        eof.integer = 0;
        eof.span.begin = end;
        eof.span.end = end;
        scannerTokens->push_back(eof);
    } catch (...) {
        delete scannerTokens;
        scannerTokens = NULL;
        throw;
    }
    std::vector<Token> tokens;
    tokens.swap(*scannerTokens);
    delete scannerTokens;
    scannerTokens = NULL;
    return tokens;
}

} // namespace cminor
