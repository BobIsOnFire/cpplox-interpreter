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
    SourceLocation sloc;
};

// FIXME: get rid of singleton instance
Scanner g_scanner; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

auto is_at_end() -> bool { return g_scanner.current >= g_scanner.source.length(); }

auto make_token(TokenType type) -> Token
{
    return Token{
            .type = type,
            .lexeme = g_scanner.source.substr(g_scanner.start, g_scanner.current - g_scanner.start),
            .sloc = g_scanner.sloc,
    };
}

auto error_token(std::string_view message) -> Token
{
    return Token{
            .type = TokenType::Error,
            .lexeme = message,
            .sloc = g_scanner.sloc,
    };
}

} // namespace

export auto init_scanner(std::string_view source) -> void
{
    g_scanner.source = source;
    g_scanner.start = 0;
    g_scanner.current = 0;

    g_scanner.sloc = {.line = 1, .column = 1};
}

export auto scan_token() -> Token
{
    using enum TokenType;

    g_scanner.start = g_scanner.current;
    if (is_at_end()) {
        return make_token(EndOfFile);
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
