module cpplox:TokenType;

import magic_enum;
import std;

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

    EndOfFile,
};

} // namespace cpplox

template <> struct std::formatter<cpplox::TokenType> : std::formatter<std::string_view>
{
    auto format(const cpplox::TokenType & type, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format(magic_enum::enum_name(type), ctx);
    }
};
