module cpplox:Environment;

import std;

import :RuntimeError;
import :Token;
import :Value;

namespace cpplox {

export class Environment
{
public:
    auto define(std::string name, Value value) -> void
    {
        m_values[std::move(name)] = std::move(value);
    }

    auto get(const Token & name) -> Value &
    {
        if (auto it = m_values.find(name.get_lexeme()); it != m_values.end()) {
            return it->second;
        }

        throw RuntimeError(name, std::format("Undefined variable '{}'.", name.get_lexeme()));
    }

private:
    std::unordered_map<std::string, Value> m_values;
};

} // namespace cpplox
