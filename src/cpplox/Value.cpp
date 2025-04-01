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

class Environment;
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
        [[nodiscard]] auto call(const Token & caller, std::span<const Value> args) const -> Value;

        std::shared_ptr<Environment> closure;
        std::function<
                Value(const Token & /* caller */,
                      Environment * /* closure */,
                      std::span<const Value> /* args */)>
                func;

        // Callable is never equal to one another (unless it's actually the same object)
        auto operator==(const Callable & other) const -> bool { return this == &other; }
    };

    class Class
    {
    public:
        Class(std::string name, std::unordered_map<std::string, ValueTypes::Callable> methods)
            : m_name(std::move(name))
            , m_methods(
                      std::make_shared<std::unordered_map<std::string, ValueTypes::Callable>>(
                              std::move(methods)
                      )
              )
        {
        }

        [[nodiscard]] auto get_name() const -> std::string_view { return m_name; }

        [[nodiscard]] auto find_method(const std::string & name) const
                -> const ValueTypes::Callable *
        {
            auto it = m_methods->find(name);
            if (it != m_methods->end()) {
                return &it->second;
            }
            return nullptr;
        }

        // Class is never equal to one another (unless it's actually the same object)
        auto operator==(const Class & other) const -> bool { return this == &other; }

    private:
        std::string m_name;
        std::shared_ptr<std::unordered_map<std::string, ValueTypes::Callable>> m_methods;
    };

    class Object
    {
    public:
        explicit Object(Class cls)
            : m_class(std::move(cls)) {};

        auto get(const Token & name) -> Value;
        auto get_method(const std::string & name) -> std::optional<ValueTypes::Callable>;
        auto set(const Token & name, Value && value) -> void;

        // Object is never equal to one another (unless it's actually the same object)
        auto operator==(const Object & other) const -> bool { return this == &other; }

        friend class std::formatter<Object>;

    private:
        Class m_class;
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
        ValueTypes::Class,
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

auto ValueTypes::Callable::call(const Token & caller, std::span<const Value> args) const -> Value
{
    return func(caller, closure.get(), args);
}

auto ValueTypes::Object::get(const Token & name) -> Value
{
    auto it = m_fields->find(name.get_lexeme());
    if (it != m_fields->end()) {
        return it->second;
    }

    auto method = get_method(name.get_lexeme());
    if (method.has_value()) {
        return std::move(method).value();
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
    return [f = std::forward<Func>(f
            )](const Token & caller, Environment * /* closure */, std::span<const Value> args) {
        if (args.size() != sizeof...(Is)) {
            throw RuntimeError(
                    caller.clone(),
                    std::format("Expected {} arguments but got {}.", sizeof...(Is), args.size())
            );
        }

        return f(args[Is]...);
    };
}

template <std::same_as<Value>... Args, NativeCallable<Args...> Func>
auto make_native_callable(const std::shared_ptr<Environment> & closure, Func && f)
        -> ValueTypes::Callable
{
    return {
            .closure = closure,
            .func = make_runtime_caller(std::forward<Func>(f), std::index_sequence_for<Args...>{}),
    };
}

} // namespace cpplox

template <> struct std::formatter<cpplox::ValueTypes::Callable> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Callable & callable, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<callable at {}>", static_cast<const void *>(&callable));
    }
};

template <> struct std::formatter<cpplox::ValueTypes::Class> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Class & cls, std::format_context & ctx) const
    {
        return std::format_to(
                ctx.out(), "<class {} at {}>", cls.get_name(), static_cast<const void *>(&cls)
        );
    }
};

template <> struct std::formatter<cpplox::ValueTypes::Object> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Object & obj, std::format_context & ctx) const
    {
        return std::format_to(
                ctx.out(),
                "<{} instance at {}>",
                obj.m_class.get_name(),
                static_cast<const void *>(&obj)
        );
    }
};

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
