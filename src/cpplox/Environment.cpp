module cpplox:Environment;

import std;

import :RuntimeError;
import :Token;
import :Value;

namespace cpplox {

class Environment
{
public:
    explicit Environment(std::shared_ptr<Environment> enclosing = nullptr)
        : m_enclosing(std::move(enclosing))
    {
    }

    auto define(std::string name, Value value) -> void
    {
        m_values.insert_or_assign(std::move(name), std::move(value));
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    auto get(const Token & name) -> Value &
    {
        if (auto it = m_values.find(name.get_lexeme()); it != m_values.end()) {
            return it->second;
        }

        if (m_enclosing != nullptr) {
            return m_enclosing->get(name);
        }

        throw RuntimeError(
                name.clone(), std::format("Undefined variable '{}'.", name.get_lexeme())
        );
    }

    auto get_at(const Token & name, std::size_t distance) -> Value &
    {
        return get_ancestor(distance)->get(name);
    }

    auto get_ancestor(std::size_t distance) -> Environment *
    {
        auto * env = this;
        for (std::size_t i = 0; i < distance; i++) {
            env = env->m_enclosing.get();
        }
        return env;
    }

    auto clear() -> void { m_values.clear(); }

private:
    std::shared_ptr<Environment> m_enclosing = nullptr;
    std::unordered_map<std::string, Value> m_values;
};

} // namespace cpplox
