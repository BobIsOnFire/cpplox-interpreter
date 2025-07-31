export module cpplox2:Value;

import std;

import magic_enum;

namespace cpplox {

export enum class ValueType : std::uint8_t {
    Boolean,
    Nil,
    Number,
};

// TODO: replace with std::variant?
export class Value
{
private:
    union ValueData
    {
        bool boolean;
        double number;
    };

    Value(ValueType type, ValueData as)
        : m_type(type)
        , m_as(as)
    {
    }

public:
    [[nodiscard]] constexpr auto get_type() const -> ValueType { return m_type; }

    // Initializers

    static constexpr auto boolean(bool value) -> Value
    {
        return {ValueType::Boolean, {.boolean = value}};
    }

    static constexpr auto nil() -> Value { return {ValueType::Nil, {.number = 0}}; }

    static constexpr auto number(double value) -> Value
    {
        return {ValueType::Number, {.number = value}};
    }

    // Casts

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    [[nodiscard]] constexpr auto as_boolean() const -> bool { return m_as.boolean; }
    [[nodiscard]] constexpr auto as_number() const -> double { return m_as.number; }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    // Queries

    [[nodiscard]] constexpr auto is(ValueType type) const -> bool { return m_type == type; }

    [[nodiscard]] constexpr auto is_boolean() const -> bool { return is(ValueType::Boolean); }
    [[nodiscard]] constexpr auto is_nil() const -> bool { return is(ValueType::Nil); }
    [[nodiscard]] constexpr auto is_number() const -> bool { return is(ValueType::Number); }

    auto operator==(const Value & other) const -> bool
    {
        if (m_type != other.m_type) {
            return false;
        }

        switch (m_type) {
        case ValueType::Boolean: return as_boolean() == other.as_boolean();
        case ValueType::Nil: return true;
        case ValueType::Number: return as_number() == other.as_number();
        }
    }

private:
    ValueType m_type;
    ValueData m_as;
};

} // namespace cpplox

template <> struct std::formatter<cpplox::ValueType> : std::formatter<std::string_view>
{
    auto format(const cpplox::ValueType & type, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format(magic_enum::enum_name(type), ctx);
    }
};

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    auto format(const cpplox::Value & value, std::format_context & ctx) const
    {
        switch (value.get_type()) {
        case cpplox::ValueType::Boolean: return std::format_to(ctx.out(), "{}", value.as_boolean());
        case cpplox::ValueType::Nil: return std::format_to(ctx.out(), "nil");
        case cpplox::ValueType::Number: return std::format_to(ctx.out(), "{}", value.as_number());
        }
    }
};
