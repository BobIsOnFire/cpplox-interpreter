module cpplox:Interpreter;

import std;

import :Diagnostics;
import :Expr;
import :ExprOperandConverter;
import :RuntimeError;
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

    auto interpret(const Expr & expr) -> void
    {
        try {
            auto value = std::visit(*this, expr);
            std::println("{}", value);
        }
        catch (const RuntimeError & err) {
            Diagnostics::instance()->runtime_error(err);
        }
    }

    auto operator()(const expr::Literal & expr) -> Value
    {
        const auto visitor = overloads{
                [](Token::EmptyLiteral) -> Value { return nullptr; },
                [](const auto & val) -> Value { return val; },
        };

        return std::visit(visitor, expr.value);
    }

    auto operator()(const expr::Grouping & expr) -> Value { return std::visit(*this, *expr.expr); }

    auto operator()(const expr::Unary & expr) -> Value
    {
        using enum TokenType;

        auto right = std::visit(*this, *expr.right);

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

        auto left = std::visit(*this, *expr.left);
        auto right = std::visit(*this, *expr.right);

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
};

} // namespace cpplox
