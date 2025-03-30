module cpplox:Resolver;

import std;

import :Diagnostics;
import :Environment;
import :Expr;
import :Interpreter;
import :Stmt;
import :Token;

namespace {

template <class T, typename Var>
concept alternative_of = requires(const Var & var) {
    []<typename... Args>
        requires(std::same_as<T, Args> || ...)
    (const std::variant<Args...> &) {}(var);
};

} // namespace

namespace cpplox {

export class Resolver
{
public:
    auto resolve(std::span<const StmtPtr> statements) -> void
    {
        for (const auto & stmt : statements) {
            resolve(*stmt);
        }
    }

    // statements

    auto operator()(const stmt::Block & block) -> void
    {
        begin_scope();
        resolve(block.stmts);
        end_scope();
    }

    auto operator()(const stmt::Class & stmt) -> void
    {
        declare(stmt.name);
        define(stmt.name);

        for (const auto & method : stmt.methods) {
            auto declaration = FunctionType::Method;
            resolve_function(std::get<stmt::Function>(*method), declaration);
        }
    }

    auto operator()(const stmt::Expression & stmt) -> void { resolve(*stmt.expr); }

    auto operator()(const stmt::Function & stmt) -> void
    {
        declare(stmt.name);
        define(stmt.name);
        resolve_function(stmt, FunctionType::Function);
    }

    auto operator()(const stmt::If & stmt) -> void
    {
        resolve(*stmt.condition);
        resolve(*stmt.then_branch);
        if (stmt.else_branch.has_value()) {
            resolve(*stmt.else_branch.value());
        }
    }

    auto operator()(const stmt::Print & stmt) -> void { resolve(*stmt.expr); }

    auto operator()(const stmt::Return & stmt) -> void
    {
        if (m_current_function == FunctionType::None) {
            Diagnostics::instance()->error(stmt.keyword, "Can't return from top-level code.");
        }
        if (stmt.value.has_value()) {
            resolve(*stmt.value.value());
        }
    }

    auto operator()(const stmt::Var & stmt) -> void
    {
        declare(stmt.name);
        if (stmt.init.has_value()) {
            resolve(*stmt.init.value());
        }
        define(stmt.name);
    }

    auto operator()(const stmt::While & stmt) -> void
    {
        resolve(*stmt.condition);
        resolve(*stmt.body);
    }

    // expressions

    auto operator()(const expr::Assign & expr) -> void
    {
        resolve(*expr.value);
        resolve_local(expr, expr.name);
    }

    auto operator()(const expr::Binary & expr) -> void
    {
        resolve(*expr.left);
        resolve(*expr.right);
    }

    auto operator()(const expr::Call & expr) -> void
    {
        resolve(*expr.callee);
        for (const auto & arg : expr.args) {
            resolve(*arg);
        }
    }

    auto operator()(const expr::Get & expr) -> void { resolve(*expr.object); }

    auto operator()(const expr::Grouping & expr) -> void { resolve(*expr.expr); }

    auto operator()(const expr::Literal & /* expr */) -> void {}

    auto operator()(const expr::Logical & expr) -> void
    {
        resolve(*expr.left);
        resolve(*expr.right);
    }

    auto operator()(const expr::Set & expr) -> void
    {
        resolve(*expr.value);
        resolve(*expr.object);
    }

    auto operator()(const expr::Unary & expr) -> void { resolve(*expr.right); }

    auto operator()(const expr::Variable & expr) -> void
    {
        if (!m_scopes.empty()) {
            auto it = m_scopes.back().find(expr.name.get_lexeme());
            if (it != m_scopes.back().end() && !it->second) {
                Diagnostics::instance()->error(
                        expr.name, "Can't read local variable in its own initializer."
                );
            }
        }

        resolve_local(expr, expr.name);
    }

private:
    enum class FunctionType : std::uint8_t
    {
        None,
        Function,
        Method,
    };

    auto resolve(const Stmt & stmt) -> void { std::visit(*this, stmt); }
    auto resolve(const Expr & expr) -> void { std::visit(*this, expr); }

    auto resolve_local(const alternative_of<Expr> auto & expr, const Token & name) -> void
    {
        auto scopes_enumerated = std::views::zip(std::views::iota(0UZ), m_scopes);
        for (const auto & [i, scope] : scopes_enumerated | std::views::reverse) {
            if (scope.contains(name.get_lexeme())) {
                Interpreter::instance()->resolve(expr, m_scopes.size() - 1 - i);
                return;
            }
        }
    }

    auto resolve_function(const stmt::Function & function, FunctionType type) -> void
    {
        auto enclosing = m_current_function;
        m_current_function = type;

        begin_scope();
        for (const auto & param : function.params) {
            declare(param);
            define(param);
        }
        resolve(function.stmts);
        end_scope();

        m_current_function = enclosing;
    }

    auto begin_scope() -> void { m_scopes.emplace_back(); }
    auto end_scope() -> void { m_scopes.pop_back(); }

    auto declare(const Token & name) -> void
    {
        if (m_scopes.empty()) {
            return;
        }
        if (m_scopes.back().contains(name.get_lexeme())) {
            Diagnostics::instance()->error(
                    name, "Already a variable with this name in this scope."
            );
        }
        m_scopes.back().emplace(name.get_lexeme(), false);
    }

    auto define(const Token & name) -> void
    {
        if (m_scopes.empty()) {
            return;
        }
        m_scopes.back().insert_or_assign(name.get_lexeme(), true);
    }

    std::vector<std::unordered_map<std::string, bool>> m_scopes;
    FunctionType m_current_function = FunctionType::None;
};

} // namespace cpplox
