module cpplox:Value;

import std;

import :RuntimeError;
import :Token;

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
        std::function<Value(std::span<const Value>)> func;

        // Callable is never equal to one another (unless it's actually the same object)
        auto operator==(const Callable & other) const -> bool { return this == &other; }
    };
    class Object
    {
    public:
        auto get(const Token & name) -> Value &;
        auto set(const Token & name, Value && value) -> void;

        // Object is never equal to one another (unless it's actually the same object)
        auto operator==(const Object & other) const -> bool { return this == &other; }

    private:
        std::shared_ptr<std::unordered_map<std::string, Value>> m_fields
                = std::make_shared<std::unordered_map<std::string, Value>>();
    };
};

using ValueVariant = std::variant<
        ValueTypes::String,
        ValueTypes::Number,
        ValueTypes::Boolean,
        ValueTypes::Null,
        ValueTypes::Callable,
        ValueTypes::Object>;

class Value : public ValueVariant
{
public:
    using variant::variant;

    // Make implicit copy private, add explicit "clone" (yeah feels like Rust, I know)
    [[nodiscard]] auto clone() const -> Value { return *this; }

    // FIXME
    // private:
    Value(const Value &) = default;
    auto operator=(const Value &) -> Value & = default;

public:
    // Core Guidelines ask to define everything else as well, so default those out.
    Value(Value &&) = default;
    auto operator=(Value &&) -> Value & = default;
    ~Value() = default;
};

auto ValueTypes::Object::get(const Token & name) -> Value &
{
    auto it = m_fields->find(name.get_lexeme());
    if (it != m_fields->end()) {
        return it->second;
    }

    throw RuntimeError(name.clone(), std::format("Undefined property '{}'.", name.get_lexeme()));
}

auto ValueTypes::Object::set(const Token & name, Value && value) -> void
{
    m_fields->insert_or_assign(name.get_lexeme(), std::move(value));
}

template <typename T, typename... Args>
concept NativeCallable = requires(T && f, Args &&... args) {
    { std::invoke(std::forward<T>(f), std::forward<Args>(args)...) } -> std::same_as<Value>;
};

template <typename Func, std::size_t... Is>
auto make_runtime_caller(Func && f, std::index_sequence<Is...> /* ids */)
{
    return [f = std::forward<Func>(f)](std::span<const Value> args) { return f(args[Is]...); };
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
                            ctx.out(), "<callable at {}>", static_cast<const void *>(&callable)
                    );
                },
                [&](const cpplox::ValueTypes::Object & obj) {
                    return std::format_to(
                            ctx.out(), "<object at {}>", static_cast<const void *>(&obj)
                    );
                },
                [&](const auto & value) { return std::format_to(ctx.out(), "{}", value); },
        };

        return std::visit(formatter, val);
    }
};
