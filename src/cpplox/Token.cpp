module cpplox:Token;

import std;

import :TokenType;

namespace cpplox {

export class Token
{
public:
    using StringLiteral = std::string;
    using NumberLiteral = double;
    using BooleanLiteral = bool;
    struct NullLiteral
    {
    };
    struct EmptyLiteral
    {
    };

    using Literal
            = std::variant<StringLiteral, NumberLiteral, BooleanLiteral, NullLiteral, EmptyLiteral>;

    Token(std::string_view lexeme,
          std::size_t line,
          TokenType type,
          Literal literal = EmptyLiteral{})
        : m_lexeme(lexeme)
        , m_line(line)
        , m_type(type)
        , m_literal(std::move(literal))
    {
    }

    friend class std::formatter<Token>;

    [[nodiscard]] auto get_lexeme() const [[clang::lifetimebound]] -> std::string_view
    {
        return m_lexeme;
    }

    [[nodiscard]] auto get_line() const -> std::size_t { return m_line; }

    [[nodiscard]] auto get_type() const -> TokenType { return m_type; }

    [[nodiscard]] auto get_literal() const -> const Literal & { return m_literal; }

private:
    std::string_view m_lexeme;
    std::size_t m_line;
    TokenType m_type;
    Literal m_literal;
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Token::NullLiteral> : std::formatter<std::string_view>
{
    auto format(const cpplox::Token::NullLiteral & /* lit */, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format("nil", ctx);
    }
};

template <> struct std::formatter<cpplox::Token::EmptyLiteral> : std::formatter<std::string_view>
{
    auto format(const cpplox::Token::EmptyLiteral & /* lit */, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format("<empty>", ctx);
    }
};

template <> struct std::formatter<cpplox::Token::Literal> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Token::Literal & literal, std::format_context & ctx) const
    {
        return std::visit(
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); },
                literal);
    }
};

template <> struct std::formatter<cpplox::Token>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Token & token, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(),
                              "Token(lexeme={}, type={}, literal={})",
                              token.m_lexeme,
                              token.m_type,
                              token.m_literal);
    }
};
