module cpplox:Interpreter;

import std;

import :Diagnostics;
import :Environment;
import :Expr;
import :ExprOperandConverter;
import :RuntimeError;
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
                std::visit(*this, *stmt);
            }
        }
        catch (const RuntimeError & err) {
            Diagnostics::instance()->runtime_error(err);
        }
    }

    auto evaluate(const Expr & expr) -> Value { return std::visit(*this, expr); }

    // statements

    auto operator()(const stmt::Print & stmt) -> void { std::println("{}", evaluate(*stmt.expr)); }

    auto operator()(const stmt::Expression & stmt) -> void { evaluate(*stmt.expr); }

    auto operator()(const stmt::Var & stmt) -> void
    {
        Value value = ValueTypes::Null{};
        if (stmt.init.has_value()) {
            value = evaluate(*stmt.init.value());
        }

        m_env.define(stmt.name.get_lexeme(), value);
    }

    // expressions

    auto operator()(const expr::Literal & expr) -> Value
    {
        const auto visitor = overloads{
                [](Token::EmptyLiteral) -> Value { return ValueTypes::Null{}; },
                [](Token::NullLiteral) -> Value { return ValueTypes::Null{}; },
                [](const auto & val) -> Value { return val; },
        };

        return std::visit(visitor, expr.value);
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
        default: throw RuntimeError(expr.op, "Unsupported unary operator.");
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
            throw RuntimeError(expr.op, "Operands must be numbers or strings.");

        case Greater: return conv.as_number(left) > conv.as_number(right);
        case GreaterEqual: return conv.as_number(left) >= conv.as_number(right);
        case Less: return conv.as_number(left) < conv.as_number(right);
        case LessEqual: return conv.as_number(left) <= conv.as_number(right);

        case BangEqual: return left != right;
        case EqualEqual: return left == right;

        default: throw RuntimeError(expr.op, "Unsupported binary operator.");
        }
    }

    auto operator()(const expr::Variable & expr) -> Value { return m_env.get(expr.name); }

private:
    auto is_truthy(const Value & val) -> bool
    {
        const auto visitor = overloads{
                [](std::nullptr_t) { return false; },
                [](bool val) { return val; },
                [](const auto &) { return true; },
        };

        return std::visit(visitor, val);
    }

    Environment m_env;
};

} // namespace cpplox
