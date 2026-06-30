module cpplox;

import std;

import :Grammar;
import :Scanner;
import :Token;

using enum cpplox::TokenType;

namespace {

// helper type for the in-place visitor
template <class... Ts>
struct overloads : Ts...
{
    using Ts::operator()...;
};

// helper for executing passed lambda at scope exit
template <typename Fn>
concept ScopeExitCallable = std::is_invocable_r_v<void, Fn>;

template <ScopeExitCallable Fn>
class ScopeExit
{
public:
    explicit ScopeExit(Fn && callable)
        : m_callable(std::move(callable))
    {
    }

    ~ScopeExit() noexcept { std::invoke(m_callable); }

    ScopeExit(const ScopeExit &) = delete;
    ScopeExit(ScopeExit &&) = delete;
    auto operator=(const ScopeExit &) const -> ScopeExit & = delete;
    auto operator=(ScopeExit &&) -> ScopeExit & = delete;

private:
    Fn m_callable;
};

// It's not possible to use a constexpr function here
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define AT_SCOPE_EXIT(body) ScopeExit([&]() -> void { body; });

constexpr const std::size_t MAX_ARGS_COUNT = 255;

} // namespace

namespace cpplox {

class Parser
{
public:
    explicit Parser(std::span<const Token> tokens)
        : m_tokens(tokens)
    {
    }

    auto parse() -> std::optional<std::vector<StmtPtr>>
    {
        std::vector<StmtPtr> stmts;
        while (!is_at_end()) {
            auto decl = declaration();
            if (decl.has_value()) {
                stmts.push_back(std::move(decl).value());
            }
        }
        // TODO: might be useful to return parsed tree even if it's invalid
        return m_had_errors ? std::nullopt : std::optional{std::move(stmts)};
    }

private:
// It's not possible to use a constexpr function here
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SLOC_BARRIER(value)                                                                        \
    auto prev_op_sloc = m_op_sloc;                                                                 \
    m_op_sloc = (value);                                                                           \
    auto sloc_exit = AT_SCOPE_EXIT(m_op_sloc = prev_op_sloc);

    [[nodiscard]] auto is_at_end() const -> bool { return peek().type == TokenType::EndOfFile; }

    [[nodiscard]] auto peek() const -> const Token & { return m_tokens[m_current]; }

    [[nodiscard]] auto previous() const -> const Token & { return m_tokens[m_current - 1]; }

    [[nodiscard]] auto is_panic_mode() const -> bool { return m_panic_mode; }

    auto advance() -> void
    {
        if (!is_at_end()) {
            m_current++;
        }
    }

    [[nodiscard]] auto check(TokenType type) -> bool
    {
        if (is_at_end()) {
            return false;
        }

        if (peek().type == TokenType::Error) {
            error(peek(), peek().lexeme);
        }

        return peek().type == type;
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
            advance();
        }
        else {
            error(peek(), error_message);
        }
        return previous();
    }

    auto error(const Token & token, std::string_view message) -> void
    {
        if (m_panic_mode) {
            return;
        }
        m_panic_mode = true;

        std::print(std::cerr, "[{}:{}] Error", token.sloc.line, token.sloc.column);

        if (token.type == TokenType::EndOfFile) {
            std::print(std::cerr, " at end");
        }
        else if (token.type != TokenType::Error) {
            std::print(std::cerr, " at '{}'", token.lexeme);
        }

        std::println(std::cerr, ": {}", message);

        m_had_errors = true;
    }

    auto synchronize() -> void
    {
        m_panic_mode = false;

        while (!is_at_end()) {
            if (previous().type == Semicolon) {
                return;
            }

            switch (peek().type) {
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
            if (peek().type == TokenType::Error) {
                error(peek(), peek().lexeme);
            }
        }
    }

    template <class ExprT, class... Args>
    auto make_unique_expr(Args &&... args) -> ExprPtr
    {
        return std::make_unique<Expr>(
                m_op_sloc, std::in_place_type<ExprT>, std::forward<Args>(args)...
        );
    }

    template <class StmtT, class... Args>
    auto make_unique_stmt(Args &&... args) -> StmtPtr
    {
        return std::make_unique<Stmt>(
                m_op_sloc, std::in_place_type<StmtT>, std::forward<Args>(args)...
        );
    }

    template <typename... Args>
    auto make_block(Args &&... args) -> StmtPtr
    {
        std::array<StmtPtr, sizeof...(args)> data{std::forward<Args>(args)...};
        return make_unique_stmt<stmt::Block>(std::vector<StmtPtr>{
                std::make_move_iterator(data.begin()),
                std::make_move_iterator(data.end()),
        });
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    auto function_node(std::string_view kind) -> stmt::Function
    {
        const auto & name = consume(Identifier, std::format("Expect {} name.", kind));
        consume(LeftParenthesis, std::format("Expect '(' after {} name.", kind));

        std::vector<Token> params;
        if (!check(RightParenthesis)) {
            do {
                if (params.size() >= MAX_ARGS_COUNT) {
                    error(peek(),
                          std::format("Cannot have more than {} parameters.", MAX_ARGS_COUNT));
                }
                params.emplace_back(consume(Identifier, "Expect parameter name."));
            } while (match(Comma));
        }
        consume(RightParenthesis, "Expect ')' after parameters.");

        consume(LeftBrace, std::format("Expect '{{' before {} body.", kind));
        return stmt::Function{
                .name = name,
                .params = std::move(params),
                .stmts = get_block_statements(),
        };
    }

    // This is recursive-descent parser, duh!
    // NOLINTBEGIN(misc-no-recursion)

    auto declaration() noexcept -> std::optional<StmtPtr>
    {
        auto decl = [&]() -> StmtPtr {
            SLOC_BARRIER(peek().sloc);

            if (match(Class)) {
                return class_declaration();
            }
            if (match(Fun)) {
                return function("function");
            }
            if (match(Var)) {
                return var_declaration();
            }

            return statement();
        }();

        if (is_panic_mode()) {
            synchronize();
            return std::nullopt;
        }

        return decl;
    }

    auto class_declaration() -> StmtPtr
    {
        const auto & name = consume(Identifier, "Expect class name.");

        auto super = match(Less)
                ? std::optional(
                          expr::Variable{
                                  .name = consume(Identifier, "Expect superclass name."),
                          }
                  )
                : std::nullopt;

        consume(LeftBrace, "Expect '{' before class body.");

        std::vector<stmt::Function> methods;
        while (!check(RightBrace) && !is_at_end()) {
            methods.push_back(function_node("method"));
        }

        consume(RightBrace, "Expect '}' after class body.");

        return make_unique_stmt<stmt::Class>(name, super, std::move(methods));
    }

    auto function(std::string_view kind) -> StmtPtr
    {
        return make_unique_stmt<stmt::Function>(function_node(kind));
    }

    auto var_declaration() -> StmtPtr
    {
        const auto & name = consume(Identifier, "Expect variable name.");

        auto init = match(Equal) ? std::optional(expression()) : std::nullopt;
        consume(Semicolon, "Expect ';' after variable declaration.");
        return make_unique_stmt<stmt::Var>(name, std::move(init));
    }

    auto statement() -> StmtPtr
    {
        SLOC_BARRIER(peek().sloc);

        if (match(For)) {
            return for_statement();
        }
        if (match(If)) {
            return if_statement();
        }
        if (match(Print)) {
            return print_statement();
        }
        if (match(Return)) {
            return return_statement();
        }
        if (match(While)) {
            return while_statement();
        }
        if (match(LeftBrace)) {
            return block();
        }

        return expression_statement();
    }

    auto for_statement() -> StmtPtr
    {
        // TODO: a separate AST node for 'for' loop instead of desugaring
        consume(LeftParenthesis, "Expect '(' after 'for'.");

        auto initializer = match(Semicolon)
                ? std::nullopt
                : std::optional(match(Var) ? var_declaration() : expression_statement());

        auto condition = check(Semicolon) ? make_unique_expr<expr::Literal>(Token{
                                                    .type = TokenType::True,
                                                    .lexeme = "true",
                                                    .sloc = previous().sloc,
                                            })
                                          : expression();
        consume(Semicolon, "Expect ';' after 'for' loop condition.");

        auto increment = check(RightParenthesis) ? std::nullopt : std::optional(expression());
        consume(RightParenthesis, "Expect ')' after 'for' clauses.");

        /*
          Desugar 'for' loop into 'while' loop.

          From:

          for (<init>; <condition>; <increment>) <body>

          Into:

          {
            <init>;
            while (<condition>) {
              { <body> }
              <increment>
            }
          }
        */

        auto body = statement();
        if (increment.has_value()) {
            body = make_block(
                    std::move(body),
                    make_unique_stmt<stmt::Expression>(std::move(increment).value())
            );
        }

        auto loop = make_unique_stmt<stmt::While>(std::move(condition), std::move(body));
        if (initializer.has_value()) {
            loop = make_block(std::move(initializer).value(), std::move(loop));
        }

        return loop;
    }

    auto if_statement() -> StmtPtr
    {
        consume(LeftParenthesis, "Expect '(' after 'if'.");
        auto condition = expression();
        consume(RightParenthesis, "Expect ')' after 'if' condition.");

        return make_unique_stmt<stmt::If>(
                std::move(condition),
                statement(),
                match(Else) ? std::optional(statement()) : std::nullopt
        );
    }

    auto print_statement() -> StmtPtr
    {
        auto value = expression();
        consume(Semicolon, "Expect ';' after value.");
        return make_unique_stmt<stmt::Print>(std::move(value));
    }

    auto return_statement() -> StmtPtr
    {
        const auto & keyword = previous();
        auto value = check(Semicolon) ? std::nullopt : std::optional(expression());

        consume(Semicolon, "Expect ';' after return value.");
        return make_unique_stmt<stmt::Return>(keyword, std::move(value));
    }

    auto while_statement() -> StmtPtr
    {
        consume(LeftParenthesis, "Expect '(' after 'while'.");
        auto condition = expression();
        consume(RightParenthesis, "Expect ')' after 'while' condition.");
        return make_unique_stmt<stmt::While>(std::move(condition), statement());
    }

    auto expression_statement() -> StmtPtr
    {
        auto expr = expression();
        consume(Semicolon, "Expect ';' after expression.");
        return make_unique_stmt<stmt::Expression>(std::move(expr));
    }

    auto block() -> StmtPtr { return make_unique_stmt<stmt::Block>(get_block_statements()); }

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

    auto expression() -> ExprPtr
    {
        SLOC_BARRIER(peek().sloc);
        return assignment();
    }

    auto assignment() -> ExprPtr
    {
        auto expr = expr_or();

        if (match(Equal)) {
            const auto & equals = previous();
            auto value = assignment();

            SLOC_BARRIER(expr->sloc);

            const auto visitor = overloads{
                    [&](expr::Variable & e) {
                        return make_unique_expr<expr::Assign>(e.name, equals, std::move(value));
                    },
                    [&](expr::Get & e) {
                        return make_unique_expr<expr::Set>(
                                std::move(e.object), e.name, std::move(value)
                        );
                    },
                    [&](auto &) {
                        error(equals, "Invalid assignment target.");
                        return std::move(expr);
                    },
            };

            return std::visit(visitor, expr->expr);
        }

        return expr;
    }

    auto expr_or() -> ExprPtr
    {
        auto expr = expr_and();
        while (match(Or)) {
            expr = make_unique_expr<expr::Logical>(std::move(expr), previous(), expr_and());
        }
        return expr;
    }

    auto expr_and() -> ExprPtr
    {
        auto expr = equality();
        while (match(And)) {
            expr = make_unique_expr<expr::Logical>(std::move(expr), previous(), equality());
        }
        return expr;
    }

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
        while (match_any(Percent, Slash, Star)) {
            expr = make_unique_expr<expr::Binary>(std::move(expr), previous(), unary());
        }
        return expr;
    }

    auto unary() -> ExprPtr
    {
        if (match_any(Bang, Minus)) {
            return make_unique_expr<expr::Unary>(previous(), unary());
        }

        return call();
    }

    auto call() -> ExprPtr
    {
        auto expr = primary();
        SLOC_BARRIER(expr->sloc);
        while (true) {
            if (match(LeftParenthesis)) {
                expr = finish_call(std::move(expr));
            }
            else if (match(Dot)) {
                const auto & name = consume(Identifier, "Expect property name after '.'.");
                m_op_sloc = name.sloc;
                expr = make_unique_expr<expr::Get>(std::move(expr), name);
            }
            else {
                break;
            }
        }

        return expr;
    }

    auto finish_call(ExprPtr callee) -> ExprPtr
    {
        std::vector<ExprPtr> args;
        if (!check(RightParenthesis)) {
            args.push_back(expression());
            while (match(Comma)) {
                if (args.size() >= MAX_ARGS_COUNT) {
                    error(peek(),
                          std::format("Cannot have more than {} arguments.", MAX_ARGS_COUNT));
                }
                args.push_back(expression());
            }
        }

        return make_unique_expr<expr::Call>(
                std::move(callee),
                consume(RightParenthesis, "Expect ')' after call arguments."),
                std::move(args)
        );
    }

    auto primary() -> ExprPtr
    {
        if (match_any(False, True, Nil, Number, String)) {
            return make_unique_expr<expr::Literal>(previous());
        }

        if (match(Super)) {
            const auto & keyword = previous();
            consume(Dot, "Expect '.' after 'super'.");

            SLOC_BARRIER(peek().sloc);

            return make_unique_expr<expr::Super>(
                    keyword, consume(Identifier, "Expect superclass method name.")
            );
        }

        if (match(This)) {
            return make_unique_expr<expr::This>(previous());
        }

        if (match(Identifier)) {
            return make_unique_expr<expr::Variable>(previous());
        }

        if (match(LeftParenthesis)) {
            auto expr = expression();
            consume(RightParenthesis, "Expect ')' after expression.");
            return make_unique_expr<expr::Grouping>(std::move(expr));
        }

        error(peek(), "Expect expression.");
        advance();
        return make_unique_expr<expr::Invalid>();
    }

    // NOLINTEND(misc-no-recursion)

    std::span<const Token> m_tokens;
    std::size_t m_current = 0;
    SourceLocation m_op_sloc = {.line = 1, .column = 1};
    bool m_panic_mode = false;
    bool m_had_errors = false;
};

// TODO: should denote failure, replace with std::expected
auto parse(std::string_view source) -> std::optional<std::vector<StmtPtr>>
{
    ScannerPtr scanner = make_scanner(source);
    // FIXME: use lazy token scanning
    std::vector<Token> tokens;
    while (true) {
        tokens.push_back(scanner->next_token());
        if (tokens.back().type == TokenType::EndOfFile) {
            break;
        }
    }

    Parser parser(tokens);
    return parser.parse();
}

} // namespace cpplox
