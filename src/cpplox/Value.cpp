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

    class Callable
    {
    public:
        using func_type = std::function<
                Value(const Token & /* caller */,
                      std::shared_ptr<Environment> /* closure */,
                      std::span<const Value> /* args */)>;

        Callable(std::shared_ptr<Environment> closure, func_type func)
            : m_closure(std::move(closure))
            , m_func(std::move(func))
        {
        }

        [[nodiscard]] auto call(const Token & caller, std::span<const Value> args) const -> Value;
        [[nodiscard]] auto bind(Value value) const -> Callable;

        // Callable is never equal to one another (unless it's actually the same object)
        auto operator==(const Callable & other) const -> bool { return m_id == other.m_id; }

        friend class std::formatter<Callable>;

    private:
        std::shared_ptr<Environment> m_closure;
        func_type m_func;

        static std::size_t s_id;
        std::size_t m_id = s_id++;
    };

    class Class
    {
    public:
        Class(std::string name,
              std::optional<Class> super,
              std::unordered_map<std::string, ValueTypes::Callable> methods)
            : m_name(std::move(name))
            , m_super(std::move(super).transform([](Class && val) {
                return std::make_shared<Class>(std::move(val));
            }))
            , m_methods(
                      std::make_shared<std::unordered_map<std::string, ValueTypes::Callable>>(
                              std::move(methods)
                      )
              )
        {
        }

        [[nodiscard]] auto get_name() const -> std::string_view { return m_name; }

        // NOLINTNEXTLINE(misc-no-recursion)
        [[nodiscard]] auto find_method(const std::string & name) const
                -> const ValueTypes::Callable *
        {
            auto it = m_methods->find(name);
            if (it != m_methods->end()) {
                return &it->second;
            }

            if (m_super.has_value()) {
                return m_super.value()->find_method(name);
            }

            return nullptr;
        }

        // Class is never equal to one another (unless it's actually the same object)
        auto operator==(const Class & other) const -> bool { return m_id == other.m_id; }

        friend class std::formatter<Class>;

    private:
        std::string m_name;
        std::optional<std::shared_ptr<Class>> m_super;
        std::shared_ptr<std::unordered_map<std::string, ValueTypes::Callable>> m_methods;

        static std::size_t s_id;
        std::size_t m_id = s_id++;
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
        auto operator==(const Object & other) const -> bool { return m_id == other.m_id; }

        friend class std::formatter<Object>;

    private:
        Class m_class;
        std::shared_ptr<std::unordered_map<std::string, Value>> m_fields
                = std::make_shared<std::unordered_map<std::string, Value>>();

        static std::size_t s_id;
        std::size_t m_id = s_id++;
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

std::size_t ValueTypes::Callable::s_id = 0;
std::size_t ValueTypes::Class::s_id = 0;
std::size_t ValueTypes::Object::s_id = 0;

auto ValueTypes::Callable::call(const Token & caller, std::span<const Value> args) const -> Value
{
    return m_func(caller, m_closure, args);
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

auto ValueTypes::Object::get_method(const std::string & name) -> std::optional<ValueTypes::Callable>
{
    const auto * method = m_class.find_method(name);
    if (method != nullptr) {
        return method->bind(*this);
    }
    return std::nullopt;
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
            )](const Token & caller,
               std::shared_ptr<Environment> /* closure */,
               std::span<const Value> args) {
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
            closure,
            make_runtime_caller(std::forward<Func>(f), std::index_sequence_for<Args...>{}),
    };
}

} // namespace cpplox

template <> struct std::formatter<cpplox::ValueTypes::Callable> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Callable & callable, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<callable #{}>", callable.m_id);
    }
};

template <> struct std::formatter<cpplox::ValueTypes::Class> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Class & cls, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<class #{} ({})>", cls.m_id, cls.m_name);
    }
};

template <> struct std::formatter<cpplox::ValueTypes::Object> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Object & obj, std::format_context & ctx) const
    {
        return std::format_to(
                ctx.out(), "<object #{} ({} instance)>", obj.m_id, obj.m_class.get_name()
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
