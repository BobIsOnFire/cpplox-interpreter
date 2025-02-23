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
        auto operator<=>(const Null &) const = default;
    };
};

export using Value = std::
        variant<ValueTypes::String, ValueTypes::Number, ValueTypes::Boolean, ValueTypes::Null>;

} // namespace cpplox

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Value & val, std::format_context & ctx) const
    {
        const auto formatter = overloads{
                [&](cpplox::ValueTypes::Null) { return std::format_to(ctx.out(), "nil"); },
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); }};

        return std::visit(formatter, val);
    }
};
