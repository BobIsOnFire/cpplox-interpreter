module cpplox:Value;

import std;

namespace {

// helper type for the in-place visitor
template <class... Ts> struct overloads : Ts...
{
    using Ts::operator()...;
};

} // namespace

namespace cpplox {

struct ValueTypes
{
    using String = std::string;
    using Number = double;
    using Boolean = bool;
    struct Null
    {
    };
};

using ValueVariant = std::
        variant<ValueTypes::String, ValueTypes::Number, ValueTypes::Boolean, ValueTypes::Null>;

struct Value : ValueVariant
{
    using variant::variant;

    // Remove implicit copy, add explicit "clone" (yeah feels like Rust, I know)
    Value(const Value &) = delete;
    auto operator=(const Value &) -> Value & = delete;

    [[nodiscard]] auto clone() const -> Value
    {
        return std::visit([](const auto & val) -> Value { return val; }, *this);
    }

    // Core Guidelines ask to define everything else as well, so default those out.
    Value(Value &&) = default;
    auto operator=(Value &&) -> Value & = default;
    ~Value() = default;
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Value & val, std::format_context & ctx) const
    {
        const auto formatter = overloads{
                [&](const cpplox::ValueTypes::Null &) { return std::format_to(ctx.out(), "nil"); },
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); },
        };

        return std::visit(formatter, val);
    }
};
