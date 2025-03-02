module cpplox:Token;

import std;

import :TokenType;

namespace {

// helper type for the in-place visitor
template <class... Ts> struct overloads : Ts...
{
    using Ts::operator()...;
};

} // namespace

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
        EmptyLiteral() = default;
        // A hack to make Literal (and Token) types implicitly non-copyable; use clone_literal below
        // for explicit copy.
        EmptyLiteral(const EmptyLiteral &) = delete;
        auto operator=(const EmptyLiteral &) -> EmptyLiteral & = delete;
        // Core Guidelines ask to define everything else as well, so default those out.
        EmptyLiteral(EmptyLiteral &&) = default;
        auto operator=(EmptyLiteral &&) -> EmptyLiteral & = default;
        ~EmptyLiteral() = default;
    };

    using Literal
            = std::variant<StringLiteral, NumberLiteral, BooleanLiteral, NullLiteral, EmptyLiteral>;

    Token(std::string lexeme, std::size_t line, TokenType type, Literal literal = EmptyLiteral{})
        : m_lexeme(std::move(lexeme))
        , m_line(line)
        , m_type(type)
        , m_literal(std::move(literal))
    {
    }

    friend class std::formatter<Token>;

    [[nodiscard]] auto get_lexeme() const -> const std::string & { return m_lexeme; }

    [[nodiscard]] auto get_line() const -> std::size_t { return m_line; }

    [[nodiscard]] auto get_type() const -> TokenType { return m_type; }

    [[nodiscard]] auto get_literal() const -> const Literal & { return m_literal; }

    [[nodiscard]] auto clone() const -> Token;

private:
    std::string m_lexeme;
    std::size_t m_line;
    TokenType m_type;
    Literal m_literal;
};

auto clone_literal(const Token::Literal & literal) -> Token::Literal
{
    return std::visit(overloads{
                              [](const Token::EmptyLiteral &) -> Token::Literal {
                                  return Token::EmptyLiteral{};
                              },
                              [](const auto & lit) -> Token::Literal { return lit; },
                      },
                      literal);
}

auto Token::clone() const -> Token
{
    return {
            m_lexeme,
            m_line,
            m_type,
            clone_literal(m_literal),
    };
}

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
