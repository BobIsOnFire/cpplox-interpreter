module cpplox:PrettyPrinter;

import std;

import :Expr;
import :Token;

namespace cpplox {

export class PrettyPrinter
{
public:
    auto operator()(const expr::Assign & expr) -> std::string
    {
        return parenthesize(std::format("assign {}", expr.name), *expr.value);
    }

    auto operator()(const expr::Binary & expr) -> std::string
    {
        return parenthesize(expr.op.get_lexeme(), *expr.left, *expr.right);
    }

    auto operator()(const expr::Call & expr) -> std::string
    {
        return std::format(
                "call {}{}",
                std::visit(PrettyPrinter{}, *expr.callee),
                parenthesize("args", expr.args)
        );
    }

    auto operator()(const expr::Grouping & expr) -> std::string
    {
        return parenthesize("group", *expr.expr);
    }

    auto operator()(const expr::Literal & expr) -> std::string
    {
        return std::format("{}", expr.value);
    }

    auto operator()(const expr::Logical & expr) -> std::string
    {
        return parenthesize(expr.op.get_lexeme(), *expr.left, *expr.right);
    }

    auto operator()(const expr::Unary & expr) -> std::string
    {
        return parenthesize(expr.op.get_lexeme(), *expr.right);
    }

    auto operator()(const expr::Variable & expr) -> std::string
    {
        return std::format("var {}", expr.name.get_literal());
    }

private:
    auto parenthesize(std::string_view name, const std::ranges::range auto & seq) -> std::string
    {
        std::stringstream exprs_joined;
        for (const auto & item : seq) {
            exprs_joined << ' ' << std::visit(PrettyPrinter{}, *item);
        }

        return std::format("({}{})", name, std::move(exprs_joined).str());
    }

    auto parenthesize(std::string_view name, const std::same_as<Expr> auto &... exprs)
            -> std::string
    {
        std::stringstream exprs_joined;
        ((exprs_joined << ' ' << std::visit(PrettyPrinter{}, exprs)), ...);

        return std::format("({}{})", name, std::move(exprs_joined).str());
    }
};

} // namespace cpplox
