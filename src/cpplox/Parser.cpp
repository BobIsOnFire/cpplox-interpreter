module cpplox:Parser;

import std;

import :Diagnostics;
import :ParserError;
import :ScopeExit;
import :Stmt;
import :Token;

using enum cpplox::TokenType;

namespace {

// helper type for the in-place visitor
template <class... Ts> struct overloads : Ts...
{
    using Ts::operator()...;
};

} // namespace

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

    auto match_any(std::same_as<TokenType> auto... types) -> bool { return (match(types) || ...); }

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

    template <typename... Args> auto make_block(Args &&... args) -> StmtPtr
    {
        std::array<StmtPtr, sizeof...(args)> data{std::forward<Args>(args)...};
        return make_unique_stmt<stmt::Block>(std::vector<StmtPtr>{
                std::make_move_iterator(data.begin()),
                std::make_move_iterator(data.end()),
        });
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

        auto init = match(Equal) ? std::optional(expression()) : std::nullopt;
        consume(Semicolon, "Expect ';' after variable declaration.");
        return make_unique_stmt<stmt::Var>(name.clone(), std::move(init));
    }

    auto statement() -> StmtPtr
    {
        if (match_any(Break, Continue)) {
            return break_statement();
        }
        if (match(For)) {
            return for_statement();
        }
        if (match(If)) {
            return if_statement();
        }
        if (match(Print)) {
            return print_statement();
        }
        if (match(While)) {
            return while_statement();
        }
        if (match(LeftBrace)) {
            return block();
        }

        return expression_statement();
    }

    auto break_statement() -> StmtPtr
    {
        const auto & token = previous();
        if (!m_parsing_loop_body) {
            throw error(token, "Unexpected loop control token outside of loop body.");
        }
        consume(Semicolon, "Expect ';' after loop control token.");
        return make_unique_stmt<stmt::LoopControl>(token.clone());
    }

    auto for_statement() -> StmtPtr
    {
        consume(LeftParenthesis, "Expect '(' after 'for'.");

        auto initializer = match(Semicolon)
                ? std::nullopt
                : std::optional(match(Var) ? var_declaration() : expression_statement());

        auto condition = check(Semicolon) ? std::nullopt : std::optional(expression());
        consume(Semicolon, "Expect ';' after 'for' loop condition.");

        auto increment = check(RightParenthesis) ? std::nullopt : std::optional(expression());
        consume(RightParenthesis, "Expect ')' after 'for' clauses.");

        return make_unique_stmt<stmt::For>(
                std::move(initializer), std::move(condition), std::move(increment), loop_body());
    }

    auto if_statement() -> StmtPtr
    {
        consume(LeftParenthesis, "Expect '(' after 'if'.");
        auto condition = expression();
        consume(RightParenthesis, "Expect ')' after 'if' condition.");

        return make_unique_stmt<stmt::If>(std::move(condition),
                                          statement(),
                                          match(Else) ? std::optional(statement()) : std::nullopt);
    }

    auto print_statement() -> StmtPtr
    {
        auto value = expression();
        consume(Semicolon, "Expect ';' after value.");
        return make_unique_stmt<stmt::Print>(std::move(value));
    }

    auto while_statement() -> StmtPtr
    {
        consume(LeftParenthesis, "Expect '(' after 'while'.");
        auto condition = expression();
        consume(RightParenthesis, "Expect ')' after 'while' condition.");
        return make_unique_stmt<stmt::While>(std::move(condition), loop_body());
    }

    auto expression_statement() -> StmtPtr
    {
        auto expr = expression();
        consume(Semicolon, "Expect ';' after expression.");
        return make_unique_stmt<stmt::Expression>(std::move(expr));
    }

    auto block() -> StmtPtr { return make_unique_stmt<stmt::Block>(get_block_statements()); }

    auto loop_body() -> StmtPtr
    {
        bool prev_loop_body = m_parsing_loop_body;
        m_parsing_loop_body = true;
        ScopeExit exit{[&]() { m_parsing_loop_body = prev_loop_body; }};
        return statement();
    }

    auto get_block_statements() -> std::vector<StmtPtr>
    {
        std::vector<StmtPtr> stmts;

        while (!check(RightBrace) && !is_at_end()) {
            auto decl = declaration();
            if (decl.has_value()) {
                stmts.push_back(std::move(decl).value());
            }
        }

        consume(RightBrace, "Expect '}' after block.");
        return stmts;
    }

    auto expression() -> ExprPtr { return assignment(); }

    auto assignment() -> ExprPtr
    {
        auto expr = expr_or();

        if (match(Equal)) {
            const auto & equals = previous();
            auto value = assignment();

            const auto visitor = overloads{
                    [&](expr::Variable & e) {
                        return make_unique_expr<expr::Assign>(e.name.clone(), std::move(value));
                    },
                    [&](auto &) {
                        error(equals, "Invalid assignment target.");
                        return std::move(expr);
                    },
            };

            return std::visit(visitor, *expr);
        }

        return expr;
    }

    auto expr_or() -> ExprPtr
    {
        auto expr = expr_and();
        while (match(Or)) {
            expr = make_unique_expr<expr::Logical>(std::move(expr), previous().clone(), expr_and());
        }
        return expr;
    }

    auto expr_and() -> ExprPtr
    {
        auto expr = equality();
        while (match(And)) {
            expr = make_unique_expr<expr::Logical>(std::move(expr), previous().clone(), equality());
        }
        return expr;
    }

    auto equality() -> ExprPtr
    {
        auto expr = comparison();
        while (match_any(BangEqual, EqualEqual)) {
            expr = make_unique_expr<expr::Binary>(
                    std::move(expr), previous().clone(), comparison());
        }
        return expr;
    }

    auto comparison() -> ExprPtr
    {
        auto expr = term();
        while (match_any(Greater, GreaterEqual, Less, LessEqual)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous().clone(), term());
        }
        return expr;
    }

    auto term() -> ExprPtr
    {
        auto expr = factor();
        while (match_any(Minus, Plus)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous().clone(), factor());
        }
        return expr;
    }

    auto factor() -> ExprPtr
    {
        auto expr = unary();
        while (match_any(Percent, Slash, Star)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous().clone(), unary());
        }
        return expr;
    }

    auto unary() -> ExprPtr
    {
        if (match_any(Bang, Minus)) {
            return make_unique_expr<expr::Unary>(previous().clone(), unary());
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
            return make_unique_expr<expr::Literal>(previous().get_literal().clone());
        }

        if (match(Identifier)) {
            return make_unique_expr<expr::Variable>(previous().clone());
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
    bool m_parsing_loop_body = false;
};

} // namespace cpplox
