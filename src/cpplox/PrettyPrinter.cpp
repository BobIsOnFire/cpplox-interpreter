module cpplox:PrettyPrinter;

import :Expr;
import :Token;
import std;

namespace cpplox {

export class PrettyPrinter
{
public:
    auto operator()(const expr::Binary & expr) -> std::string
    {
        return parenthesize(expr.op.get_lexeme(), *expr.left, *expr.right);
    }

    auto operator()(const expr::Grouping & expr) -> std::string
    {
        return parenthesize("group", *expr.expr);
    }

    auto operator()(const expr::Literal & expr) -> std::string
    {
        return std::format("{}", expr.value);
    }

    auto operator()(const expr::Unary & expr) -> std::string
    {
        return parenthesize(expr.op.get_lexeme(), *expr.right);
    }

private:
    template <class... Exprs>
    auto parenthesize(std::string_view name, const Exprs &... exprs) -> std::string
    {
        static_assert((std::is_same_v<Expr, Exprs> || ...));

        std::stringstream exprs_joined;
        ((exprs_joined << ' ' << std::visit(PrettyPrinter{}, exprs)), ...);

        return std::format("({}{})", name, std::move(exprs_joined).str());
    }
};

} // namespace cpplox
