module cpplox:Value;

import std;

import :RuntimeError;
import :Stmt;
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

    class Function
    {
    public:
        Function(
                const stmt::Function & node,
                std::shared_ptr<Environment> closure,
                bool is_initializer = false
        )
            : m_node(&node)
            , m_closure(std::move(closure))
            , m_is_initializer(is_initializer)
        {
        }

        [[nodiscard]] auto get_node() const -> const stmt::Function & { return *m_node; }
        [[nodiscard]] auto get_closure() const -> const std::shared_ptr<Environment> &
        {
            return m_closure;
        }
        [[nodiscard]] auto is_initializer() const -> bool { return m_is_initializer; }

        // Function is never equal to one another (unless it's actually the same object)
        auto operator==(const Function & other) const -> bool { return m_id == other.m_id; }

        friend class std::formatter<Function>;

    private:
        // TODO: probably should make interpreter AST-walking non-const and just move the node
        // into the value here
        const stmt::Function * m_node;
        std::shared_ptr<Environment> m_closure;
        bool m_is_initializer;

        static std::size_t s_id;
        std::size_t m_id = s_id++;
    };

    class NativeFunction
    {
    public:
        using func_type = std::function<Value(std::span<const Value> /* args */)>;

        NativeFunction(std::string_view name, std::size_t arity, func_type func)
            : m_name(name)
            , m_arity(arity)
            , m_func(std::move(func))
        {
        }

        [[nodiscard]] auto get_name() const -> std::string_view { return m_name; }
        [[nodiscard]] auto get_arity() const -> std::size_t { return m_arity; }
        [[nodiscard]] auto get_function() const -> const func_type & { return m_func; }

        auto operator==(const NativeFunction & other) const -> bool
        {
            return m_name == other.m_name;
        }

    private:
        std::string_view m_name;
        std::size_t m_arity;
        func_type m_func;
    };

    class Class
    {
    public:
        using methods_type = std::unordered_map<std::string, Function>;

        Class(std::string name, std::optional<Class> super, methods_type methods)
            : m_name(std::move(name))
            , m_super(std::move(super).transform([](Class && val) {
                return std::make_shared<Class>(std::move(val));
            }))
            , m_methods(std::make_shared<methods_type>(std::move(methods)))
        {
        }

        [[nodiscard]] auto get_name() const -> std::string_view { return m_name; }
        [[nodiscard]] auto get_super() const -> const std::optional<std::shared_ptr<Class>> &
        {
            return m_super;
        }
        [[nodiscard]] auto get_methods() const -> const methods_type & { return *m_methods; }

        // Class is never equal to one another (unless it's actually the same object)
        auto operator==(const Class & other) const -> bool { return m_id == other.m_id; }

        friend class std::formatter<Class>;

    private:
        std::string m_name;
        std::optional<std::shared_ptr<Class>> m_super;
        std::shared_ptr<methods_type> m_methods;

        static std::size_t s_id;
        std::size_t m_id = s_id++;
    };

    class Object
    {
    public:
        using fields_type = std::unordered_map<std::string, Value>;

        explicit Object(Class cls)
            : m_class(std::move(cls)) {};

        [[nodiscard]] auto get_class() const -> const Class & { return m_class; }
        [[nodiscard]] auto get_fields() const -> const fields_type &;
        [[nodiscard]] auto get_fields() -> fields_type &;

        // Object is never equal to one another (unless it's actually the same object)
        auto operator==(const Object & other) const -> bool { return m_id == other.m_id; }

        friend class std::formatter<Object>;

    private:
        Class m_class;
        std::shared_ptr<fields_type> m_fields = std::make_shared<fields_type>();

        static std::size_t s_id;
        std::size_t m_id = s_id++;
    };
};

using ValueVariant = std::variant<
        ValueTypes::String,
        ValueTypes::Number,
        ValueTypes::Boolean,
        ValueTypes::Null,
        ValueTypes::Function,
        ValueTypes::NativeFunction,
        ValueTypes::Class,
        ValueTypes::Object>;

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

std::size_t ValueTypes::Function::s_id = 0;
std::size_t ValueTypes::Class::s_id = 0;
std::size_t ValueTypes::Object::s_id = 0;

auto ValueTypes::Object::get_fields() const -> const ValueTypes::Object::fields_type &
{
    return *m_fields;
}

auto ValueTypes::Object::get_fields() -> ValueTypes::Object::fields_type & { return *m_fields; }

template <typename T, typename... Args>
concept NativeCallable = requires(T && f, Args &&... args) {
    { std::invoke(std::forward<T>(f), std::forward<Args>(args)...) } -> std::same_as<Value>;
};

template <typename Func, std::size_t... Is>
auto make_runtime_caller(Func && f, std::index_sequence<Is...> /* ids */)
        -> ValueTypes::NativeFunction::func_type
{
    return [f = std::forward<Func>(f)](std::span<const Value> args) { return f(args[Is]...); };
}

template <std::same_as<Value>... Args, NativeCallable<Args...> Func>
auto make_native_function(std::string_view name, Func && f) -> ValueTypes::NativeFunction
{
    return {
            name,
            sizeof...(Args),
            make_runtime_caller(std::forward<Func>(f), std::index_sequence_for<Args...>{}),
    };
}

} // namespace cpplox

template <> struct std::formatter<cpplox::ValueTypes::Function> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::Function & func, std::format_context & ctx) const
    {
        return std::format_to(
                ctx.out(), "<fun #{} ({})>", func.m_id, func.m_node->name.get_lexeme()
        );
    }
};

template <>
struct std::formatter<cpplox::ValueTypes::NativeFunction> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::ValueTypes::NativeFunction & func, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<native fun ({})>", func.get_name());
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
