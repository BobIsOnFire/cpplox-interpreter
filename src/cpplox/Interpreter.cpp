module;

#include <cassert>

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

template <class T, typename Var>
concept alternative_of = requires(const Var & var) {
    []<typename... Args>
        requires(std::same_as<T, Args> || ...)
    (const std::variant<Args...> &) {}(var);
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

    auto resolve(const alternative_of<Expr> auto & expr, std::size_t depth) -> void
    {
        m_locals.insert_or_assign(static_cast<const void *>(&expr), depth);
    }

    // statements

    auto operator()(const stmt::Block & block) -> void
    {
        auto block_env = std::make_shared<Environment>(m_env.get());
        execute_block(block.stmts, block_env);

        // Callables defined in the block are holding a reference to the block environment,
        // which causes cyclic dependency. Explicitly clear out environment before releasing
        // it, there's nothing that could reference this block after this point.
        // TODO: shouldn't the same be done for function scope below? Why does it work there???
        block_env->clear();
        assert(block_env.use_count() == 1);
    }

    auto operator()(const stmt::Class & stmt) -> void
    {
        std::unordered_map<std::string, ValueTypes::Callable> methods;

        for (const auto & method : stmt.methods) {
            bool is_initializer = method.name.get_lexeme() == "init";
            methods.emplace(
                    method.name.get_lexeme(),
                    create_callable(method.name, method.params, method.stmts, is_initializer)
            );
        }

        m_env->define(
                stmt.name.get_lexeme(),
                ValueTypes::Class(stmt.name.get_lexeme(), std::move(methods))
        );
    }

    auto operator()(const stmt::Function & stmt) -> void
    {
        m_env->define(stmt.name.get_lexeme(), create_callable(stmt.name, stmt.params, stmt.stmts));
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
        throw Return(stmt.value.transform([this](auto & v) { return evaluate(*v); }
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
                stmt.init.transform([this](auto & v) { return evaluate(*v); }
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

    auto operator()(const expr::Variable & expr) -> Value
    {
        return lookup_variable(expr.name, expr).clone();
    }

    auto operator()(const expr::This & expr) -> Value
    {
        return lookup_variable(expr.keyword, expr).clone();
    }

    auto operator()(const expr::Assign & expr) -> Value
    {
        auto value = evaluate(*expr.value);
        lookup_variable(expr.name, expr) = value.clone();
        return value;
    }

    auto operator()(const expr::Call & expr) -> Value
    {
        auto callee = evaluate(*expr.callee);

        auto args = expr.args // range magic to evaluate all args in order
                | std::views::transform([&](const auto & e) { return evaluate(*e); })
                | std::ranges::to<std::vector>();

        return invoke_value(callee, args, expr.paren);
    }

    auto operator()(const expr::Get & expr) -> Value
    {
        auto object = evaluate(*expr.object);
        auto visitor = overloads{
                [&](ValueTypes::Object & obj) { return obj.get(expr.name); },
                [&](auto &&) -> Value {
                    throw RuntimeError(expr.name.clone(), "Only objects have properties.");
                },
        };

        return std::visit(visitor, object);
    }

    auto operator()(const expr::Set & expr) -> Value
    {
        auto object = evaluate(*expr.object);
        auto visitor = overloads{
                [&](ValueTypes::Object & obj) {
                    auto value = evaluate(*expr.value);
                    obj.set(expr.name, value.clone());
                    return value;
                },
                [&](auto &&) -> Value {
                    throw RuntimeError(expr.name.clone(), "Only objects have properties.");
                },
        };

        return std::visit(visitor, object);
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

    auto create_callable(
            const Token & name,
            std::span<const Token> params,
            std::span<const StmtPtr> stmts,
            bool is_initializer = false
    ) -> ValueTypes::Callable
    {
        return {
                .closure = m_env,
                // FIXME: In REPL mode, if function was defined in a different line input,
                // "stmts" might already be destroyed when we actually get to calling it.
                // Need to have statements globally available somehow to support REPL mode.
                .func
                = [this, &name, params, stmts, is_initializer](
                          const Token & caller, Environment * closure, std::span<const Value> args
                  ) -> Value {
                    if (args.size() != params.size()) {
                        throw RuntimeError(
                                caller.clone(),
                                std::format(
                                        "Expected {} arguments but got {}.",
                                        params.size(),
                                        args.size()
                                )
                        );
                    }

                    auto func_env = std::make_shared<Environment>(closure);

                    for (std::size_t i = 0; i < params.size(); i++) {
                        func_env->define(params[i].get_lexeme(), args[i].clone());
                    }

                    Value return_value = ValueTypes::Null{};
                    try {
                        execute_block(stmts, func_env);
                    }
                    catch (Return & ret) {
                        return_value = std::move(ret).value();
                    }

                    if (is_initializer) {
                        Token this_tok("this", name.get_line(), TokenType::This);
                        return_value = func_env->get_at(this_tok, 0);
                    }

                    return return_value;
                },
        };
    }

    auto invoke_value(Value & value, std::span<const Value> args, const Token & token) -> Value
    {
        auto visitor = overloads{
                [&](ValueTypes::Callable & callable) { return callable.call(token, args); },
                [&](ValueTypes::Class & cls) { return create_class_instance(cls, args, token); },
                [&](auto &&) -> Value {
                    throw RuntimeError(token.clone(), "Can only call functions and classes.");
                },
        };

        return std::visit(visitor, value);
    }

    auto lookup_variable(const Token & name, const alternative_of<Expr> auto & expr) -> Value &
    {
        auto it = m_locals.find(static_cast<const void *>(&expr));
        if (it != m_locals.end()) {
            return m_env->get_at(name, it->second);
        }
        return m_globals->get(name);
    }

    auto init_globals() -> void
    {
        m_globals->define("clock", make_native_callable(m_env, []() -> Value {
                              using namespace std::chrono;
                              return static_cast<double>(
                                      duration_cast<seconds>(system_clock::now().time_since_epoch())
                                              .count()
                              );
                          }));
    }

    auto create_class_instance(
            const ValueTypes::Class & cls, std::span<const Value> args, const Token & token
    ) -> Value
    {
        auto obj = ValueTypes::Object(cls);

        // initialize object
        auto init = obj.get_method("init");
        if (init.has_value()) {
            return init.value().call(token, args);
        }
        return obj;
    }

    std::unordered_map<const void *, std::size_t> m_locals;
    std::shared_ptr<Environment> m_globals = std::make_shared<Environment>();
    std::shared_ptr<Environment> m_env = m_globals;
};

} // namespace cpplox
