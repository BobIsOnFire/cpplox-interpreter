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
private:
    class Return : std::exception
    {
    public:
        explicit Return(Value value)
            : m_value(std::move(value))
        {
        }
        template <class Self> [[nodiscard]] auto value(this Self && self) -> auto &&
        {
            return std::forward<Self>(self).m_value;
        }

    private:
        Value m_value;
    };

public:
    Interpreter() { init_globals(); }

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
        catch (const RuntimeError & err) {
            Diagnostics::instance()->runtime_error(err);
        }
    }

    auto evaluate(const Expr & expr) -> Value { return std::visit(*this, expr); }

    auto execute(const Stmt & stmt) -> void { std::visit(*this, stmt); }

    // statements

    auto operator()(const stmt::Block & block) -> void
    {
        execute_block(block.stmts, std::make_shared<Environment>(m_env.get()));
    }

    auto operator()(const stmt::Function & stmt) -> void
    {
        m_env->define(
                stmt.name.get_lexeme(),
                ValueTypes::Callable{
                        .arity = stmt.params.size(),
                        .func = [this, &stmt, env = m_env](std::span<const Value> args) -> Value {
                            auto func_env = std::make_shared<Environment>(env.get());

                            for (std::size_t i = 0; i < stmt.params.size(); i++) {
                                func_env->define(stmt.params[i].get_lexeme(), args[i].clone());
                            }
                            try {
                                execute_block(stmt.stmts, func_env);
                            }
                            catch (Return & ret) {
                                return std::move(ret).value();
                            }
                            return ValueTypes::Null{};
                        },
                }
        );
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

    auto operator()(const stmt::Print & stmt) -> void { std::println("{}", evaluate(*stmt.expr)); }

    auto operator()(const stmt::Return & stmt) -> void
    {
        throw Return(stmt.value.transform(
                                       [this](auto & v) { return evaluate(*v); }
        ).value_or(ValueTypes::Null{}));
    }

    auto operator()(const stmt::While & stmt) -> void
    {
        while (is_truthy(evaluate(*stmt.condition))) {
            execute(*stmt.body);
        }
    }

    auto operator()(const stmt::Expression & stmt) -> void { evaluate(*stmt.expr); }

    auto operator()(const stmt::Var & stmt) -> void
    {
        m_env->define(
                stmt.name.get_lexeme(),
                stmt.init.transform(
                                 [this](auto & v) { return evaluate(*v); }
                ).value_or(ValueTypes::Null{})
        );
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

    auto operator()(const expr::Call & expr) -> Value
    {
        auto callee = evaluate(*expr.callee);

        auto args = expr.args // range magic to evaluate all args in order
                | std::views::transform([&](const auto & e) { return evaluate(*e); })
                | std::ranges::to<std::vector>();

        auto visitor = overloads{
                [&](ValueTypes::Callable & callable) {
                    return invoke_callable(callable, args, expr.paren);
                },
                [&](auto &&) -> Value {
                    throw RuntimeError(expr.paren.clone(), "Can only call functions and classes.");
                },
        };

        return std::visit(visitor, callee);
    }

private:
    auto execute_block(std::span<const StmtPtr> stmts, std::shared_ptr<Environment> env) -> void
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

    auto invoke_callable(
            ValueTypes::Callable & callable, std::span<const Value> args, const Token & token
    ) -> Value
    {
        if (args.size() != callable.arity) {
            throw RuntimeError(
                    token.clone(),
                    std::format("Expected {} arguments but got {}.", callable.arity, args.size())
            );
        }
        return callable.func(args);
    }

    auto init_globals() -> void
    {
        m_globals->define(
                "clock", make_native_callable([]() -> Value {
                    using namespace std::chrono;
                    return static_cast<double>(
                            duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
                    );
                })
        );
    }

    std::shared_ptr<Environment> m_globals = std::make_shared<Environment>();
    std::shared_ptr<Environment> m_env = m_globals;
};

} // namespace cpplox
