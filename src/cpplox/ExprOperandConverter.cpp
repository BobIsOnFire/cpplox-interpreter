module cpplox:ExprOperandConverter;

import std;

import :RuntimeError;
import :Token;
import :Value;

namespace cpplox {

export class ExprOperandConverter
{
public:
    explicit ExprOperandConverter(const Token & token)
        : m_token(token)
    {
    }
    ~ExprOperandConverter() = default;

    // Class stores a reference, explicitly forbid it from copying/moving
    ExprOperandConverter(ExprOperandConverter const &) = delete;
    ExprOperandConverter(ExprOperandConverter &&) = delete;
    auto operator=(ExprOperandConverter const &) -> ExprOperandConverter & = delete;
    auto operator=(ExprOperandConverter &&) -> ExprOperandConverter & = delete;

    auto as_number(Value & value) -> value::Number &
    {
        return as<value::Number>(value, "Operand must be number.");
    }

    auto as_string(Value & value) -> value::String &
    {
        return as<value::String>(value, "Operand must be string.");
    }

    template <typename T> auto as(Value & value, std::string_view error_message) -> T &
    {
        try {
            return std::get<T>(value);
        }
        catch (std::bad_variant_access &) {
            throw RuntimeError(m_token.clone(), error_message);
        }
    }

private:
    const Token & m_token;
};

} // namespace cpplox