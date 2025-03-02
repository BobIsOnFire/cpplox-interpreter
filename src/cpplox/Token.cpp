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
        auto operator<=>(const NullLiteral &) const = default;
    };
    struct EmptyLiteral
    {
        auto operator<=>(const EmptyLiteral &) const = default;
    };

    using LiteralVariant
            = std::variant<StringLiteral, NumberLiteral, BooleanLiteral, NullLiteral, EmptyLiteral>;

    class Literal : public LiteralVariant
    {
    public:
        using variant::variant;

        // Make implicit copy private, add explicit "clone" (yeah feels like Rust, I know)
        [[nodiscard]] auto clone() const -> Literal { return *this; }

    private:
        Literal(const Literal &) = default;
        auto operator=(const Literal &) -> Literal & = default;

    public:
        // Core Guidelines ask to define everything else as well, so default those out.
        Literal(Literal &&) = default;
        auto operator=(Literal &&) -> Literal & = default;
        ~Literal() = default;
    };

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

    [[nodiscard]] auto clone() const -> Token
    {
        return {
                m_lexeme,
                m_line,
                m_type,
                m_literal.clone(),
        };
    }

private:
    std::string m_lexeme;
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
