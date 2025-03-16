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

class Value;

struct ValueTypes
{
    using String = std::string;
    using Number = double;
    using Boolean = bool;
    struct Null
    {
        auto operator<=>(const Null &) const = default;
    };
    struct Callable
    {
        std::size_t arity;
        std::function<Value(const std::vector<Value> &)> func;

        // Callable is never equal to one another (unless it's actually the same object)
        auto operator==(const Callable & other) const -> bool { return this == &other; }
    };
};

using ValueVariant = std::variant<ValueTypes::Callable,
                                  ValueTypes::String,
                                  ValueTypes::Number,
                                  ValueTypes::Boolean,
                                  ValueTypes::Null>;

class Value : public ValueVariant
{
public:
    using variant::variant;

    // Make implicit copy private, add explicit "clone" (yeah feels like Rust, I know)
    [[nodiscard]] auto clone() const -> Value { return *this; }

private:
    Value(const Value &) = default;
    auto operator=(const Value &) -> Value & = default;

public:
    // Core Guidelines ask to define everything else as well, so default those out.
    Value(Value &&) = default;
    auto operator=(Value &&) -> Value & = default;
    ~Value() = default;
};

template <typename T, typename... Args>
concept NativeCallable = requires(T && f, Args &&... args) {
    { std::invoke(std::forward<T>(f), std::forward<Args>(args)...) } -> std::same_as<Value>;
};

template <typename Func, std::size_t... Is>
auto make_runtime_caller(Func && f, std::index_sequence<Is...> /* ids */)
{
    return [f = std::forward<Func>(f)](const std::vector<Value> & args) { return f(args[Is]...); };
}

template <std::same_as<Value>... Args, NativeCallable<Args...> Func>
auto make_native_callable(Func && f) -> ValueTypes::Callable
{
    return {
            .arity = sizeof...(Args),
            .func = make_runtime_caller(std::forward<Func>(f), std::index_sequence_for<Args...>{}),
    };
}

} // namespace cpplox

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::Value & val, std::format_context & ctx) const
    {
        const auto formatter = overloads{
                [&](const cpplox::ValueTypes::Null &) { return std::format_to(ctx.out(), "nil"); },
                [&](const cpplox::ValueTypes::Callable & callable) {
                    return std::format_to(
                            ctx.out(), "<callable at {}>", static_cast<const void *>(&callable));
                },
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); },
        };

        return std::visit(formatter, val);
    }
};
