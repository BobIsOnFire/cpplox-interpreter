module cpplox:Parser;

import std;

import :Expr;
import :Lox;
import :ParserError;
import :Token;

using enum cpplox::TokenType;

namespace cpplox {

export class Parser
{
public:
    explicit Parser(std::vector<Token> tokens)
        : m_tokens(std::move(tokens))
    {
    }

    auto parse() -> std::unique_ptr<Expr>
    {
        try {
            return expression();
        }
        catch (const ParserError & error) {
            return make_unique_expr<expr::Literal>(Token::EmptyLiteral{});
        }
    }

private:
    [[nodiscard]] auto is_at_end() const -> bool
    {
        return peek().get_type() == TokenType::EndOfFile;
    }

    [[nodiscard]] auto peek() const -> const Token & { return m_tokens[m_current]; }

    [[nodiscard]] auto previous() const -> const Token & { return m_tokens[m_current - 1]; }

    auto advance() -> const Token &
    {
        if (!is_at_end()) {
            m_current++;
        }
        return previous();
    }

    [[nodiscard]] auto check(TokenType type) const -> bool
    {
        if (is_at_end()) {
            return false;
        }

        return peek().get_type() == type;
    }

    auto match(TokenType type) -> bool
    {
        if (check(type)) {
            advance();
            return true;
        }
        return false;
    }

    auto match_any(auto... types) -> bool
    {
        static_assert((std::is_same_v<TokenType, decltype(types)> || ...));
        return (match(types) || ...);
    }

    auto consume(TokenType type, std::string_view error_message) -> const Token &
    {
        if (check(type)) {
            return advance();
        }
        throw error(peek(), error_message);
    }

    auto error(const Token & token, std::string_view message) -> ParserError
    {
        Lox::instance()->error(token, message);
        return ParserError(message);
    }

    auto synchronize() -> void
    {
        advance();

        while (!is_at_end()) {
            if (previous().get_type() == Semicolon) {
                return;
            }

            switch (peek().get_type()) {
            case Class:
            case Fun:
            case Var:
            case For:
            case If:
            case While:
            case Print:
            case Return: return;
            default: // keep going
            }

            advance();
        }
    }

    // This is recursive-descent parser, duh!
    // NOLINTBEGIN(misc-no-recursion)

    auto expression() -> std::unique_ptr<Expr> { return equality(); }

    auto equality() -> std::unique_ptr<Expr>
    {
        auto expr = comparison();
        while (match_any(BangEqual, EqualEqual)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), comparison());
        }
        return expr;
    }

    auto comparison() -> std::unique_ptr<Expr>
    {
        auto expr = term();
        while (match_any(Greater, GreaterEqual, Less, LessEqual)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), term());
        }
        return expr;
    }

    auto term() -> std::unique_ptr<Expr>
    {
        auto expr = factor();
        while (match_any(Minus, Plus)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), factor());
        }
        return expr;
    }

    auto factor() -> std::unique_ptr<Expr>
    {
        auto expr = unary();
        while (match_any(Slash, Star)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), unary());
        }
        return expr;
    }

    auto unary() -> std::unique_ptr<Expr>
    {
        if (match_any(Bang, Minus)) {
            return make_unique_expr<expr::Unary>(previous(), unary());
        }

        return primary();
    }

    auto primary() -> std::unique_ptr<Expr>
    {
        if (match(False)) {
            return make_unique_expr<expr::Literal>(false);
        }

        if (match(True)) {
            return make_unique_expr<expr::Literal>(true);
        }

        if (match(Nil)) {
            return make_unique_expr<expr::Literal>(nullptr);
        }

        if (match_any(Number, String)) {
            return make_unique_expr<expr::Literal>(previous().get_literal());
        }

        if (match(LeftParenthesis)) {
            auto expr = expression();
            consume(RightParenthesis, "Expect ')' after expression.");
            return make_unique_expr<expr::Grouping>(std::move(expr));
        }

        throw error(peek(), "Expect expression.");
    }

    // NOLINTEND(misc-no-recursion)

    std::vector<Token> m_tokens;
    std::size_t m_current = 0;
};

} // namespace cpplox
