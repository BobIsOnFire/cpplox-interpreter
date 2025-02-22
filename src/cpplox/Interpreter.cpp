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
    using ObjectData = std::variant<std::string, double, bool, std::nullptr_t>;
    using Object = std::unique_ptr<ObjectData>;

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
        case Bang: *right = is_truthy(right); break;
        case Minus: *right = std::get<double>(*right); break;
        default: // ???
        }

        return right;
    }

    auto operator()(const expr::Binary & expr) -> Object
    {
        using enum TokenType;

        auto left = std::visit(*this, *expr.left);
        auto right = std::visit(*this, *expr.right);

        switch (expr.op.get_type()) {
        case Minus: *left = std::get<double>(*left) - std::get<double>(*right); break;
        case Slash: *left = std::get<double>(*left) / std::get<double>(*right); break;
        case Star: *left = std::get<double>(*left) * std::get<double>(*right); break;

        case Plus:
            if (std::holds_alternative<double>(*left)) {
                *left = std::get<double>(*left) + std::get<double>(*right);
            }
            else {
                std::get<std::string>(*left).append(std::get<std::string>(*right));
            }
            break;

        case Greater: *left = std::get<double>(*left) > std::get<double>(*right); break;
        case GreaterEqual: *left = std::get<double>(*left) >= std::get<double>(*right); break;
        case Less: *left = std::get<double>(*left) < std::get<double>(*right); break;
        case LessEqual: *left = std::get<double>(*left) <= std::get<double>(*right); break;

        case BangEqual: *left = *left != *right; break;
        case EqualEqual: *left = *left == *right; break;

        default: // ???
        }

        return left;
    }

private:
    template <class... Args> static auto make_object(Args &&... args) -> Object
    {
        return std::make_unique<ObjectData>(std::forward<Args>(args)...);
    }

    auto is_truthy(const Object & obj) -> bool
    {
        const auto visitor = overloads{
                [](std::nullptr_t) { return false; },
                [](bool val) { return val; },
                [](const auto &) { return true; },
        };

        return std::visit(visitor, *obj);
    }

    auto is_equal(const Object & left, const Object & right) -> bool
    {
        return std::visit(
                [&](const auto & left_val) {
                    return std::holds_alternative<std::decay_t<decltype(left_val)>>(*right)
                            && left_val == std::get<std::decay_t<decltype(left_val)>>(*right);
                },
                *left);
    }
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Interpreter::Object> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Interpreter::Object & obj, std::format_context & ctx) const
    {
        return std::visit(
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); }, *obj);
    }
};
