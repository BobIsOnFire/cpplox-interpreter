module cpplox:Interpreter;

import std;

import :Diagnostics;
import :Expr;
import :RuntimeError;
import :Token;

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
    using Object = std::variant<std::string, double, bool, std::nullptr_t>;

    static auto instance() -> Interpreter *
    {
        static Interpreter s_instance;
        return &s_instance;
    }

    auto interpret(const Expr & expr) -> void;

    auto operator()(const expr::Literal & expr) -> Object
    {
        const auto visitor = overloads{
                [](Token::EmptyLiteral) { return make_object(nullptr); },
                [](auto val) { return make_object(std::move(val)); },
        };

        return std::visit(visitor, expr.value);
    }

    auto operator()(const expr::Grouping & expr) -> Object { return std::visit(*this, *expr.expr); }

    auto operator()(const expr::Unary & expr) -> Object
    {
        using enum TokenType;

        auto right = std::visit(*this, *expr.right);

        switch (expr.op.get_type()) {
        case Bang: return !is_truthy(right);
        case Minus: return -as_number(right, expr.op);
        default: throw RuntimeError(expr.op, "Unsupported unary operator.");
        }
    }

    auto operator()(const expr::Binary & expr) -> Object
    {
        using enum TokenType;

        auto left = std::visit(*this, *expr.left);
        auto right = std::visit(*this, *expr.right);

        switch (expr.op.get_type()) {
        case Minus: return as_number(left, expr.op) - as_number(right, expr.op);
        case Slash: return as_number(left, expr.op) / as_number(right, expr.op);
        case Star: return as_number(left, expr.op) * as_number(right, expr.op);

        case Plus:
            if (std::holds_alternative<double>(left)) {
                return as_number(left, expr.op) + as_number(right, expr.op);
            }
            else if (std::holds_alternative<std::string>(left)) {
                as_string(left, expr.op).append(as_string(right, expr.op));
                return left;
            }
            throw RuntimeError(expr.op, "Operands must be numbers or strings.");

        case Greater: return as_number(left, expr.op) > as_number(right, expr.op);
        case GreaterEqual: return as_number(left, expr.op) >= as_number(right, expr.op);
        case Less: return as_number(left, expr.op) < as_number(right, expr.op);
        case LessEqual: return as_number(left, expr.op) <= as_number(right, expr.op);

        case BangEqual: return left != right;
        case EqualEqual: return left == right;

        default: throw RuntimeError(expr.op, "Unsupported binary operator.");
        }
    }

private:
    template <class... Args> static auto make_object(Args &&... args) -> Object
    {
        return Object{std::forward<Args>(args)...};
    }

    auto is_truthy(const Object & obj) -> bool
    {
        const auto visitor = overloads{
                [](std::nullptr_t) { return false; },
                [](bool val) { return val; },
                [](const auto &) { return true; },
        };

        return std::visit(visitor, obj);
    }

    auto as_number(Object & obj, const Token & token) -> double &
    {
        return as<double>(obj, token, "Operand must be a number.");
    }

    auto as_string(Object & obj, const Token & token) -> std::string &
    {
        return as<std::string>(obj, token, "Operand must be a string.");
    }

    template <typename T>
    auto as(Object & obj, const Token & token, std::string_view error_message) -> T &
    {
        try {
            return std::get<T>(obj);
        }
        catch (std::bad_variant_access &) {
            throw RuntimeError(token, error_message);
        }
    }
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Interpreter::Object> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Interpreter::Object & obj, std::format_context & ctx) const
    {
        const auto formatter = overloads{
                [&](std::nullptr_t) { return std::format_to(ctx.out(), "nil"); },
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); }};

        return std::visit(formatter, obj);
    }
};

namespace cpplox {

auto Interpreter::interpret(const Expr & expr) -> void
{
    try {
        auto value = std::visit(*this, expr);
        std::println("{}", value);
    }
    catch (const RuntimeError & err) {
        Diagnostics::instance()->runtime_error(err);
    }
}

} // namespace cpplox
