module cpplox:Token;

import :TokenType;

import std;

namespace cpplox {

export class Token
{
public:
    struct EmptyLiteral
    {
    };
    using Literal = std::variant<std::string, double, EmptyLiteral>;

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

private:
    std::string_view m_lexeme;
    std::size_t m_line;
    TokenType m_type;
    Literal m_literal;
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Token::EmptyLiteral> : std::formatter<std::string_view>
{
    auto format(const cpplox::Token::EmptyLiteral & /* lit */, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format("<empty>", ctx);
    }
};

template <> struct std::formatter<cpplox::Token>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Token & token, std::format_context & ctx) const
    {
        return std::visit(
                [&](const auto & literal) {
                    return std::format_to(ctx.out(),
                                          "Token(lexeme={}, type={}, literal={})",
                                          token.m_lexeme,
                                          token.m_type,
                                          literal);
                },
                token.m_literal);
    }
};
