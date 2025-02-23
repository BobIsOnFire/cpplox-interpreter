module cpplox:Interpreter;

import std;

import :Expr;
import :Token;

namespace cpplox {

namespace {

// helper type for the in-place visitor
template <class... Ts> struct overloads : Ts...
{
    using Ts::operator()...;
};

} // namespace

export class Interpreter
{
public:
    using Object = std::variant<std::string, double, bool, std::nullptr_t>;

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
        case Bang: return is_truthy(right);
        case Minus: return -std::get<double>(right);
        default: // ???
        }

        std::unreachable();
    }

    auto operator()(const expr::Binary & expr) -> Object
    {
        using enum TokenType;

        auto left = std::visit(*this, *expr.left);
        auto right = std::visit(*this, *expr.right);

        switch (expr.op.get_type()) {
        case Minus: return std::get<double>(left) - std::get<double>(right);
        case Slash: return std::get<double>(left) / std::get<double>(right);
        case Star: return std::get<double>(left) * std::get<double>(right);

        case Plus:
            if (std::holds_alternative<double>(left)) {
                return std::get<double>(left) + std::get<double>(right);
            }
            else {
                std::get<std::string>(left).append(std::get<std::string>(right));
                return left;
            }

        case Greater: return std::get<double>(left) > std::get<double>(right);
        case GreaterEqual: return std::get<double>(left) >= std::get<double>(right);
        case Less: return std::get<double>(left) < std::get<double>(right);
        case LessEqual: return std::get<double>(left) <= std::get<double>(right);

        case BangEqual: return left != right;
        case EqualEqual: return left == right;

        default: // ???
        }

        return left;
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
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Interpreter::Object> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Interpreter::Object & obj, std::format_context & ctx) const
    {
        return std::visit(
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); }, obj);
    }
};
