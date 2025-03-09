module cpplox:Interpreter;

import std;

import :Diagnostics;
import :Environment;
import :Expr;
import :ExprOperandConverter;
import :RuntimeError;
import :ScopeExit;
import :Stmt;
import :Value;

namespace {

// helper type for the in-place visitor
template <class... Ts> struct overloads : Ts...
{
    using Ts::operator()...;
};

} // namespace

namespace cpplox {

export class Interpreter
{
public:
    static auto instance() -> Interpreter *
    {
        static Interpreter s_instance;
        return &s_instance;
    }

    auto interpret(std::span<StmtPtr> statements) -> void
    {
        try {
            for (const auto & stmt : statements) {
                execute(*stmt);
            }
        }
        catch (const LoopControlExc & loop_control) {
            Diagnostics::instance()->runtime_error(RuntimeError(
                    loop_control.token().clone(),
                    "Executed loop control statement outside of loop body, possibly AST bug."));
        }
        catch (const RuntimeError & err) {
            Diagnostics::instance()->runtime_error(err);
        }
    }

    auto evaluate(const Expr & expr) -> Value { return std::visit(*this, expr); }

    auto execute(const Stmt & stmt) -> void { std::visit(*this, stmt); }

    // statements

    auto operator()(const stmt::Block & block) -> void
    {
        execute_block(block.stmts, std::make_unique<Environment>(&*m_env));
    }

    auto operator()(const stmt::For & stmt) -> void
    {
        if (stmt.initializer.has_value()) {
            execute(*stmt.initializer.value());
        }
        while (!stmt.condition.has_value() || is_truthy(evaluate(*stmt.condition.value()))) {
            ScopeExit loop_increment = stmt.increment.has_value()
                    ? ScopeExit{[&]() { evaluate(*stmt.increment.value()); }}
                    : ScopeExit{[]() {}};
            try {
                execute(*stmt.body);
            }
            catch (const LoopControlExc & control) {
                if (control.token().get_type() == TokenType::Break) {
                    break;
                }
                continue;
            }
        }
    }

    auto operator()(const stmt::If & stmt) -> void
    {
        if (is_truthy(evaluate(*stmt.condition))) {
            execute(*stmt.then_branch);
        }
        else if (stmt.else_branch.has_value()) {
            execute(*stmt.else_branch.value());
        }
    }

    auto operator()(const stmt::LoopControl & stmt) -> void { throw LoopControlExc{stmt.self}; }

    auto operator()(const stmt::Print & stmt) -> void { std::println("{}", evaluate(*stmt.expr)); }

    auto operator()(const stmt::While & stmt) -> void
    {
        while (is_truthy(evaluate(*stmt.condition))) {
            try {
                execute(*stmt.body);
            }
            catch (const LoopControlExc & control) {
                if (control.token().get_type() == TokenType::Break) {
                    break;
                }
                continue;
            }
        }
    }

    auto operator()(const stmt::Expression & stmt) -> void { evaluate(*stmt.expr); }

    auto operator()(const stmt::Var & stmt) -> void
    {
        Value value = ValueTypes::Null{};
        if (stmt.init.has_value()) {
            value = evaluate(*stmt.init.value());
        }

        m_env->define(stmt.name.get_lexeme(), std::move(value));
    }

    // expressions

    auto operator()(const expr::Literal & expr) -> Value
    {
        const auto visitor = overloads{
                [](const Token::EmptyLiteral &) -> Value { return ValueTypes::Null{}; },
                [](const Token::NullLiteral &) -> Value { return ValueTypes::Null{}; },
                [](const auto & val) -> Value { return val; },
        };

        return std::visit(visitor, expr.value);
    }

    auto operator()(const expr::Logical & expr) -> Value
    {
        auto left = evaluate(*expr.left);
        // logical operators short-circuit; do not evaluate right operand until
        // checking condition on the left
        if (expr.op.get_type() == TokenType::Or) {
            if (is_truthy(left)) {
                return left;
            }
        }
        else {
            if (!is_truthy(left)) {
                return left;
            }
        }
        return evaluate(*expr.right);
    }

    auto operator()(const expr::Grouping & expr) -> Value { return evaluate(*expr.expr); }

    auto operator()(const expr::Unary & expr) -> Value
    {
        using enum TokenType;

        auto right = evaluate(*expr.right);

        ExprOperandConverter conv(expr.op);

        switch (expr.op.get_type()) {
        case Bang: return !is_truthy(right);
        case Minus: return -conv.as_number(right);
        default: throw RuntimeError(expr.op.clone(), "Unsupported unary operator.");
        }
    }

    auto operator()(const expr::Binary & expr) -> Value
    {
        using enum TokenType;

        auto left = evaluate(*expr.left);
        auto right = evaluate(*expr.right);

        ExprOperandConverter conv(expr.op);

        switch (expr.op.get_type()) {
        case Minus: return conv.as_number(left) - conv.as_number(right);
        case Percent: return std::fmod(conv.as_number(left), conv.as_number(right));
        case Slash: return conv.as_number(left) / conv.as_number(right);
        case Star: return conv.as_number(left) * conv.as_number(right);

        case Plus:
            if (std::holds_alternative<double>(left)) {
                return conv.as_number(left) + conv.as_number(right);
            }
            else if (std::holds_alternative<std::string>(left)) {
                conv.as_string(left).append(conv.as_string(right));
                return left;
            }
            throw RuntimeError(expr.op.clone(), "Operands must be numbers or strings.");

        case Greater: return conv.as_number(left) > conv.as_number(right);
        case GreaterEqual: return conv.as_number(left) >= conv.as_number(right);
        case Less: return conv.as_number(left) < conv.as_number(right);
        case LessEqual: return conv.as_number(left) <= conv.as_number(right);

        case BangEqual: return left != right;
        case EqualEqual: return left == right;

        default: throw RuntimeError(expr.op.clone(), "Unsupported binary operator.");
        }
    }

    auto operator()(const expr::Variable & expr) -> Value { return m_env->get(expr.name).clone(); }

    auto operator()(const expr::Assign & expr) -> Value
    {
        auto value = evaluate(*expr.value);
        m_env->assign(expr.name, value.clone());
        return value;
    }

private:
    class LoopControlExc : std::exception
    {
    public:
        explicit LoopControlExc(const Token & token)
            : m_token(token)
        {
        }
        ~LoopControlExc() override = default;

        // Class stores a reference, explicitly forbid it from copying/moving
        LoopControlExc(LoopControlExc const &) = delete;
        LoopControlExc(LoopControlExc &&) = delete;
        auto operator=(LoopControlExc const &) -> LoopControlExc & = delete;
        auto operator=(LoopControlExc &&) -> LoopControlExc & = delete;

        [[nodiscard]] auto token() const -> const Token & { return m_token; }

    private:
        const Token & m_token;
    };

    auto execute_block(const std::vector<StmtPtr> & stmts, std::unique_ptr<Environment> env) -> void
    {
        std::swap(m_env, env);
        ScopeExit exit{[&]() { std::swap(m_env, env); }};

        for (const auto & stmt : stmts) {
            execute(*stmt);
        }
    }

    auto is_truthy(const Value & val) -> bool
    {
        const auto visitor = overloads{
                [](ValueTypes::Null) { return false; },
                [](ValueTypes::Boolean val) { return val; },
                [](const auto &) { return true; },
        };

        return std::visit(visitor, val);
    }

    std::unique_ptr<Environment> m_env = std::make_unique<Environment>();
};

} // namespace cpplox
