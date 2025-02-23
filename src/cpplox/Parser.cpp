module cpplox:Parser;

import std;

import :Diagnostics;
import :ParserError;
import :Stmt;
import :Token;

using enum cpplox::TokenType;

namespace cpplox {

export class Parser
{
public:
    explicit Parser(std::span<const Token> tokens)
        : m_tokens(tokens)
    {
    }

    auto parse() -> std::vector<StmtPtr>
    {
        std::vector<StmtPtr> stmts;
        while (!is_at_end()) {
            auto decl = declaration();
            if (decl.has_value()) {
                stmts.push_back(std::move(decl).value());
            }
        }
        return stmts;
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
        Diagnostics::instance()->error(token, message);
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

    auto declaration() noexcept -> std::optional<StmtPtr>
    {
        try {
            if (match(Var)) {
                return var_declaration();
            }

            return statement();
        }
        catch (const ParserError & error) {
            synchronize();
            return std::nullopt;
        }
    }

    auto var_declaration() -> StmtPtr
    {
        const auto & name = consume(Identifier, "Expect variable name.");

        std::optional<ExprPtr> init = std::nullopt;
        if (match(Equal)) {
            init = expression();
        }

        consume(Semicolon, "Expect ';' after variable declaration.");
        return make_unique_stmt<stmt::Var>(name, std::move(init));
    }

    auto statement() -> StmtPtr
    {
        if (match(Print)) {
            return print_statement();
        }

        return expression_statement();
    }

    auto print_statement() -> StmtPtr
    {
        auto value = expression();
        consume(Semicolon, "Expect ';' after value.");
        return make_unique_stmt<stmt::Print>(std::move(value));
    }

    auto expression_statement() -> StmtPtr
    {
        auto expr = expression();
        consume(Semicolon, "Expect ';' after expression.");
        return make_unique_stmt<stmt::Expression>(std::move(expr));
    }

    auto expression() -> ExprPtr { return equality(); }

    auto equality() -> ExprPtr
    {
        auto expr = comparison();
        while (match_any(BangEqual, EqualEqual)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), comparison());
        }
        return expr;
    }

    auto comparison() -> ExprPtr
    {
        auto expr = term();
        while (match_any(Greater, GreaterEqual, Less, LessEqual)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), term());
        }
        return expr;
    }

    auto term() -> ExprPtr
    {
        auto expr = factor();
        while (match_any(Minus, Plus)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), factor());
        }
        return expr;
    }

    auto factor() -> ExprPtr
    {
        auto expr = unary();
        while (match_any(Slash, Star)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), unary());
        }
        return expr;
    }

    auto unary() -> ExprPtr
    {
        if (match_any(Bang, Minus)) {
            return make_unique_expr<expr::Unary>(previous(), unary());
        }

        return primary();
    }

    auto primary() -> ExprPtr
    {
        if (match(False)) {
            return make_unique_expr<expr::Literal>(false);
        }

        if (match(True)) {
            return make_unique_expr<expr::Literal>(true);
        }

        if (match(Nil)) {
            return make_unique_expr<expr::Literal>(Token::NullLiteral{});
        }

        if (match_any(Number, String)) {
            return make_unique_expr<expr::Literal>(previous().get_literal());
        }

        if (match(Identifier)) {
            return make_unique_expr<expr::Variable>(previous());
        }

        if (match(LeftParenthesis)) {
            auto expr = expression();
            consume(RightParenthesis, "Expect ')' after expression.");
            return make_unique_expr<expr::Grouping>(std::move(expr));
        }

        throw error(peek(), "Expect expression.");
    }

    // NOLINTEND(misc-no-recursion)

    std::span<const Token> m_tokens;
    std::size_t m_current = 0;
};

} // namespace cpplox
