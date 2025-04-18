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

template <typename T, typename... Args>
concept NativeCallable = requires(T && f, Args &&... args) {
    { std::invoke(std::forward<T>(f), std::forward<Args>(args)...) } -> std::same_as<cpplox::Value>;
};

template <typename Func, std::size_t... Is>
auto make_runtime_caller(Func && f, std::index_sequence<Is...> /* ids */)
        -> cpplox::value::NativeFunction::func_type
{
    return [f = std::forward<Func>(f)](std::span<const cpplox::Value> args) {
        return f(args[Is]...);
    };
}

template <std::same_as<cpplox::Value>... Args, NativeCallable<Args...> Func>
auto make_native_function(std::string_view name, Func && f) -> cpplox::value::NativeFunctionPtr
{
    return std::make_shared<cpplox::value::NativeFunction>(
            name,
            sizeof...(Args),
            make_runtime_caller(std::forward<Func>(f), std::index_sequence_for<Args...>{})
    );
}

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
        auto block_env = std::make_shared<Environment>(m_env);
        execute_block(block.stmts, block_env);
    }

    auto operator()(const stmt::Class & stmt) -> void
    {
        auto super = stmt.super.transform([this](const expr::Variable & super) {
            return std::visit(
                    overloads{
                            [](value::ClassPtr cls) { return cls; },
                            [&](auto &&) -> value::ClassPtr {
                                throw RuntimeError(
                                        super.name.clone(), "Superclass must be a class."
                                );
                            },
                    },
                    this->operator()(super)
            );
        });

        std::unordered_map<std::string, value::FunctionPtr> methods;

        {
            auto class_env = std::make_shared<Environment>(m_env);
            std::swap(m_env, class_env);
            ScopeExit exit{[&]() { std::swap(m_env, class_env); }};

            if (super.has_value()) {
                m_env->define("super", super.value());
            }

            for (const auto & method : stmt.methods) {
                bool is_initializer = method.name.get_lexeme() == "init";
                methods.emplace(method.name.get_lexeme(), create_function(method, is_initializer));
            }
        }

        m_env->define(
                stmt.name.get_lexeme(),
                std::make_shared<value::Class>(
                        stmt.name.get_lexeme(), std::move(methods), std::move(super)
                )
        );
    }

    auto operator()(const stmt::Function & stmt) -> void
    {
        m_env->define(stmt.name.get_lexeme(), create_function(stmt));
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
        ).value_or(value::Null{}));
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
                ).value_or(value::Null{})
        );
    }

    // expressions

    auto operator()(const expr::Literal & expr) -> Value
    {
        const auto visitor = overloads{
                [](const Token::EmptyLiteral &) -> Value { return value::Null{}; },
                [](const Token::NullLiteral &) -> Value { return value::Null{}; },
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

    auto operator()(const expr::Super & expr) -> Value
    {
        std::size_t distance = m_locals.at(&expr);
        auto super = std::visit(
                overloads{
                        [](value::ClassPtr & cls) { return cls; },
                        [&](auto &&) -> value::ClassPtr {
                            throw RuntimeError(expr.keyword.clone(), "'super' is not a class.");
                        },
                },
                m_env->get_at(expr.keyword, distance)
        );

        Token this_tok("this", expr.keyword.get_line(), TokenType::This);
        auto this_obj = m_env->get_at(this_tok, distance - 1).clone();

        const auto * method = lookup_method(super, expr.method.get_lexeme());
        if (method == nullptr) {
            throw RuntimeError(
                    expr.method.clone(),
                    std::format("Undefined property '{}'.", expr.method.get_lexeme())
            );
        }

        return bind_function(*method, std::move(this_obj));
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
                [&](value::ObjectPtr & obj) { return lookup_field(obj, expr.name); },
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
                [&](value::ObjectPtr & obj) {
                    auto value = evaluate(*expr.value);
                    obj->fields.insert_or_assign(expr.name.get_lexeme(), value.clone());
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
                [](value::Null) { return false; },
                [](value::Boolean val) { return val; },
                [](const auto &) { return true; },
        };

        return std::visit(visitor, val);
    }

    auto invoke(value::FunctionPtr & func, std::span<const Value> args, const Token & caller)
            -> Value
    {
        const auto & node = *func->node;
        check_arity(caller, node.params.size(), args.size());

        auto func_env = std::make_shared<Environment>(func->closure);

        for (std::size_t i = 0; i < node.params.size(); i++) {
            func_env->define(node.params[i].get_lexeme(), args[i].clone());
        }

        Value return_value = value::Null{};
        try {
            execute_block(node.stmts, func_env);
        }
        catch (Return & ret) {
            return_value = std::move(ret).value();
        }

        if (func->is_initializer) {
            Token this_tok("this", node.name.get_line(), TokenType::This);
            return_value = func_env->get_at(this_tok, 0).clone();
        }

        return return_value;
    }

    auto
    invoke(value::NativeFunctionPtr & native, std::span<const Value> args, const Token & caller)
            -> Value
    {
        check_arity(caller, native->arity, args.size());
        return native->func(args);
    }

    auto invoke_value(Value & value, std::span<const Value> args, const Token & caller) -> Value
    {
        auto visitor = overloads{
                [&](value::FunctionPtr & func) { return invoke(func, args, caller); },
                [&](value::NativeFunctionPtr & func) { return invoke(func, args, caller); },
                [&](value::ClassPtr & cls) { return create_class_instance(cls, args, caller); },
                [&](auto &&) -> Value {
                    throw RuntimeError(caller.clone(), "Can only call functions and classes.");
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

    auto lookup_field(const value::ObjectPtr & obj, const Token & name) -> Value
    {
        const auto & fields = obj->fields;
        auto it = fields.find(name.get_lexeme());
        if (it != fields.end()) {
            return it->second.clone();
        }

        auto method = lookup_method(obj, name.get_lexeme());
        if (method.has_value()) {
            return std::move(method).value();
        }

        throw RuntimeError(
                name.clone(), std::format("Undefined property '{}'.", name.get_lexeme())
        );
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    auto lookup_method(const value::ClassPtr & cls, const std::string & name)
            -> const value::FunctionPtr *
    {
        const auto & methods = cls->methods;
        auto it = methods.find(name);
        if (it != methods.end()) {
            return &it->second;
        }

        const auto & super = cls->super;
        if (super.has_value()) {
            return lookup_method(super.value(), name);
        }

        return nullptr;
    }

    auto lookup_method(const value::ObjectPtr & obj, const std::string & name)
            -> std::optional<value::FunctionPtr>
    {
        const auto * method = lookup_method(obj->cls, name);
        if (method != nullptr) {
            return bind_function(*method, obj);
        }
        return std::nullopt;
    }

    auto init_globals() -> void
    {
        m_globals->define("clock", make_native_function("clock", []() -> Value {
                              using namespace std::chrono;
                              return static_cast<double>(
                                      duration_cast<seconds>(system_clock::now().time_since_epoch())
                                              .count()
                              );
                          }));
    }

    auto create_function(const stmt::Function & node, bool is_initializer = false)
            -> value::FunctionPtr
    {
        return std::make_shared<value::Function>(&node, m_env, is_initializer);
    }

    auto create_class_instance(
            const value::ClassPtr & cls, std::span<const Value> args, const Token & caller
    ) -> Value
    {
        static constexpr std::string init = "init";

        auto obj = std::make_shared<value::Object>(cls);

        auto maybe_init = lookup_method(obj, init);
        if (maybe_init.has_value()) {
            return invoke(maybe_init.value(), args, caller);
        }

        check_arity(caller, 0, args.size());
        return obj;
    }

    auto check_arity(const Token & caller, std::size_t arity, std::size_t args_len) -> void
    {
        if (args_len != arity) {
            throw RuntimeError(
                    caller.clone(),
                    std::format("Expected {} arguments but got {}.", arity, args_len)
            );
        }
    }

    auto bind_function(const value::FunctionPtr & func, Value this_obj) -> value::FunctionPtr
    {
        auto this_closure = std::make_shared<Environment>(func->closure);
        this_closure->define("this", std::move(this_obj));
        return std::make_shared<value::Function>(func->node, this_closure, func->is_initializer);
    }

    std::unordered_map<const void *, std::size_t> m_locals;
    std::shared_ptr<Environment> m_globals = std::make_shared<Environment>();
    std::shared_ptr<Environment> m_env = m_globals;
};

} // namespace cpplox
