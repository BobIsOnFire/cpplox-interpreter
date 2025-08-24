export module cpplox2:Scanner;

import std;

import :SourceLocation;

import magic_enum;

namespace cpplox {

export enum class TokenType : std::uint8_t {
    // Single-character tokens
    LeftParenthesis,  // (
    RightParenthesis, // )
    LeftBrace,        // {
    RightBrace,       // }
    Comma,            // ,
    Dot,              // .
    Minus,            // -
    Percent,          // %
    Plus,             // +
    Semicolon,        // ;
    Slash,            // /
    Star,             // *

    // One or two character tokens
    Bang,         // !
    BangEqual,    // !=
    Equal,        // =
    EqualEqual,   // ==
    Greater,      // >
    GreaterEqual, // >=
    Less,         // <
    LessEqual,    // <=

    // Literals
    Identifier, // function names, variable names
    String,     // "hello"
    Number,     // 4, 10.01

    // Keywords
    And,
    Class,
    Else,
    False,
    Fun,
    For,
    If,
    Nil,
    Or,
    Print,
    Return,
    Super,
    This,
    True,
    Var,
    While,

    Error,
    EndOfFile,
};

export struct Token
{
    TokenType type;
    std::string_view lexeme; // TODO: lifetime checks?
    SourceLocation sloc;
};

export struct Scanner
{
    std::string_view source;
    std::size_t start;
    std::size_t current;
    SourceLocation start_sloc;
    SourceLocation sloc;
};

// FIXME: get rid of singleton instance
Scanner g_scanner; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

auto is_digit(char c) -> bool { return c >= '0' && c <= '9'; }
auto is_alpha(char c) -> bool { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

auto is_at_end() -> bool { return g_scanner.current >= g_scanner.source.length(); }

auto advance() -> char
{
    g_scanner.current++;
    g_scanner.sloc.column++;
    return g_scanner.source[g_scanner.current - 1];
}

auto peek() -> char { return g_scanner.source[g_scanner.current]; }

auto peek_next() -> char
{
    if (is_at_end()) {
        return '\0';
    }
    return g_scanner.source[g_scanner.current + 1];
}

auto match(char expected) -> bool
{
    if (is_at_end()) {
        return false;
    }
    if (g_scanner.source[g_scanner.current] != expected) {
        return false;
    }
    advance();
    return true;
}

auto skip_whitespace() -> void
{
    while (!is_at_end()) {
        char c = peek();
        switch (c) {
        case '\n':
            // TODO: move line tracking into advance() as well
            g_scanner.sloc.line++;
            g_scanner.sloc.column = 0;
            [[fallthrough]];
        case ' ':
        case '\r':
        case '\t': advance(); break;
        case '/':
            if (peek_next() == '/') {
                while (!is_at_end() && peek() != '\n') {
                    advance();
                }
            }
            else {
                return;
            }
            break;
        default: return;
        }
    }
}

auto get_lexeme() -> std::string_view
{
    return g_scanner.source.substr(g_scanner.start, g_scanner.current - g_scanner.start);
}

auto make_token(TokenType type) -> Token
{
    return Token{
            .type = type,
            .lexeme = get_lexeme(),
            .sloc = g_scanner.start_sloc,
    };
}

auto error_token(std::string_view message) -> Token
{
    return Token{
            .type = TokenType::Error,
            .lexeme = message,
            .sloc = g_scanner.start_sloc,
    };
}

auto check_keyword(std::string_view keyword, TokenType type) -> TokenType
{
    if (get_lexeme() == keyword) {
        return type;
    }
    return TokenType::Identifier;
}

auto identifier_type() -> TokenType
{
    using enum TokenType;
    auto lexeme = get_lexeme();
    switch (lexeme[0]) {
    case 'a': return check_keyword("and", And);
    case 'c': return check_keyword("class", Class);
    case 'e': return check_keyword("else", Else);
    case 'f':
        if (lexeme.length() > 1) {
            switch (lexeme[1]) {
            case 'a': return check_keyword("false", False);
            case 'o': return check_keyword("for", For);
            case 'u': return check_keyword("fun", Fun);
            default: return Identifier;
            }
        }
        break;
    case 'i': return check_keyword("if", If);
    case 'n': return check_keyword("nil", Nil);
    case 'o': return check_keyword("or", Or);
    case 'p': return check_keyword("print", Print);
    case 'r': return check_keyword("return", Return);
    case 's': return check_keyword("super", Super);
    case 't':
        if (lexeme.length() > 1) {
            switch (lexeme[1]) {
            case 'h': return check_keyword("this", This);
            case 'r': return check_keyword("true", True);
            default: return Identifier;
            }
        }
        break;
    case 'v': return check_keyword("var", Var);
    case 'w': return check_keyword("while", While);
    default: return Identifier;
    }
    std::unreachable();
}

auto string() -> Token
{
    while (!is_at_end() && peek() != '"') {
        if (peek() == '\n') {
            g_scanner.sloc.line++;
            g_scanner.sloc.column = 0;
        }
        advance();
    }

    if (is_at_end()) {
        return error_token("Unterminated string.");
    }

    advance(); // consume closing quote
    return make_token(TokenType::String);
}

auto number() -> Token
{
    while (!is_at_end() && is_digit(peek())) {
        advance();
    }

    if (g_scanner.current + 1 < g_scanner.source.length() && peek() == '.'
        && is_digit(peek_next())) {
        advance(); // consume dot
        while (!is_at_end() && is_digit(peek())) {
            advance();
        }
    }

    return make_token(TokenType::Number);
}

auto identifier() -> Token
{
    while (!is_at_end() && (is_alpha(peek()) || is_digit(peek()) || peek() == '_')) {
        advance();
    }
    return make_token(identifier_type());
}

} // namespace

export auto init_scanner(std::string_view source) -> void
{
    g_scanner.source = source;
    g_scanner.start = 0;
    g_scanner.current = 0;

    g_scanner.start_sloc = {.line = 1, .column = 1};
    g_scanner.sloc = {.line = 1, .column = 1};
}

export auto scan_token() -> Token
{
    using enum TokenType;

    skip_whitespace();

    g_scanner.start = g_scanner.current;
    g_scanner.start_sloc = g_scanner.sloc;
    if (is_at_end()) {
        return make_token(EndOfFile);
    }

    char c = advance();
    if (is_digit(c)) {
        return number();
    }
    if (is_alpha(c) || c == '_') {
        return identifier();
    }

    switch (c) { // NOLINT(bugprone-switch-missing-default-case,hicpp-multiway-paths-covered)
    case '(': return make_token(LeftParenthesis);
    case ')': return make_token(RightParenthesis);
    case '{': return make_token(LeftBrace);
    case '}': return make_token(RightBrace);
    case ';': return make_token(Semicolon);
    case ',': return make_token(Comma);
    case '.': return make_token(Dot);
    case '-': return make_token(Minus);
    case '+': return make_token(Plus);
    case '/': return make_token(Slash);
    case '*': return make_token(Star);
    case '!': return make_token(match('=') ? BangEqual : Bang);
    case '=': return make_token(match('=') ? EqualEqual : Equal);
    case '<': return make_token(match('=') ? LessEqual : Less);
    case '>': return make_token(match('=') ? GreaterEqual : Greater);
    case '"': return string();
    }

    return error_token("Unexpected character.");
}

} // namespace cpplox

template <> struct std::formatter<cpplox::TokenType> : std::formatter<std::string_view>
{
    auto format(const cpplox::TokenType & type, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format(magic_enum::enum_name(type), ctx);
    }
};
