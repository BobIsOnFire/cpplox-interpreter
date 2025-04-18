module cpplox:ValueTypes;

import :Stmt;
import :Token;

namespace cpplox {
class Environment;
class Value;
} // namespace cpplox

namespace cpplox {
namespace value {
using String = std::string;
using Number = double;
using Boolean = bool;

struct Null
{
    auto operator<=>(const Null &) const = default;
};

struct Function
{
    const stmt::Function * node;
    std::shared_ptr<Environment> closure;
    bool is_initializer;
};
using FunctionPtr = std::shared_ptr<Function>;

struct NativeFunction
{
    using func_type = std::function<Value(std::span<const Value>)>;

    std::string_view name;
    std::size_t arity;
    func_type func;
};
using NativeFunctionPtr = std::shared_ptr<NativeFunction>;

struct Class
{
    using methods_type = std::unordered_map<std::string, std::shared_ptr<Function>>;
    using super_type = std::optional<std::shared_ptr<Class>>;

    std::string name;
    methods_type methods;
    super_type super;
};
using ClassPtr = std::shared_ptr<Class>;

struct Object
{
    using fields_type = std::unordered_map<std::string, Value>;

    std::shared_ptr<value::Class> cls;
    fields_type fields;
};
using ObjectPtr = std::shared_ptr<Object>;

} // namespace value

using ValueVariant = std::variant<
        value::String,
        value::Number,
        value::Boolean,
        value::Null,
        value::FunctionPtr,
        value::NativeFunctionPtr,
        value::ClassPtr,
        value::ObjectPtr>;

} // namespace cpplox

template <> struct std::formatter<cpplox::value::Function> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::value::Function & func, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<fun {}>", func.node->name.get_lexeme());
    }
};

template <> struct std::formatter<cpplox::value::NativeFunction> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::value::NativeFunction & func, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<native fun {}>", func.name);
    }
};

template <> struct std::formatter<cpplox::value::Class> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::value::Class & cls, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<class {}>", cls.name);
    }
};

template <> struct std::formatter<cpplox::value::Object> : std::formatter<std::string_view>
{
    constexpr auto parse(std::format_parse_context & ctx) { return ctx.begin(); }

    auto format(const cpplox::value::Object & obj, std::format_context & ctx) const
    {
        return std::format_to(ctx.out(), "<class {} instance>", obj.cls->name);
    }
};
