export module cpplox:Token;

import std;

import :EnumFormatter;
import :SourceLocation;

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

} // namespace cpplox

template <> struct std::formatter<cpplox::TokenType> : cpplox::EnumFormatter<cpplox::TokenType>
{
};
