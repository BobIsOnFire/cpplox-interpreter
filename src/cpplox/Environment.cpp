module cpplox:Environment;

import std;

import :RuntimeError;
import :Token;
import :Value;

namespace cpplox {

export class Environment
{
public:
    explicit Environment(Environment * enclosing = nullptr)
        : m_enclosing(enclosing)
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

    auto assign(const Token & name, Value value) -> void { get(name) = std::move(value); }

private:
    Environment * m_enclosing = nullptr;
    std::unordered_map<std::string, Value> m_values;
};

} // namespace cpplox
