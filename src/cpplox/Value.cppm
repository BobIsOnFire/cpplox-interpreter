export module cpplox:Value;

import std;

import :EnumFormatter;
import :ObjectFwd;

import magic_enum;

namespace cpplox {

// TODO: replace with std::variant?
export class Value
{
public:
    using NativeFn = Value (*)(std::span<const Value>);

    enum class ValueType : std::uint8_t
    {
        Boolean,
        Nil,
        Number,
        Obj,
    };

private:
    union ValueData
    {
        bool boolean;
        double number;
        Obj * obj;
    };

    Value(ValueType type, ValueData as)
        : m_type(type)
        , m_as(as)
    {
    }

public:
    [[nodiscard]] constexpr auto get_type() const -> ValueType { return m_type; }

    // Initializers

    static auto boolean(bool value) -> Value { return {ValueType::Boolean, {.boolean = value}}; }

    static auto nil() -> Value { return {ValueType::Nil, {.number = 0}}; }

    static auto number(double value) -> Value { return {ValueType::Number, {.number = value}}; }

    // Do we need Obj/ObjString stuff in public members? Can we hide everything
    // behind our Obj * field?
    static auto obj(Obj * obj) -> Value { return {ValueType::Obj, {.obj = obj}}; }

    static auto string(std::string) -> Value;
    static auto upvalue(Value *) -> Value;
    static auto function(std::string) -> Value;
    static auto closure(ObjFunction *) -> Value;
    static auto native(NativeFn) -> Value;
    static auto cls(ObjString *) -> Value;
    static auto instance(ObjClass *) -> Value;
    static auto bound_method(Value, ObjClosure *) -> Value;

    // Casts

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    [[nodiscard]] auto as_boolean() const -> bool { return m_as.boolean; }
    [[nodiscard]] auto as_number() const -> double { return m_as.number; }
    [[nodiscard]] auto as_obj() const -> Obj * { return m_as.obj; }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    [[nodiscard]] auto as_objstring() const -> ObjString *;
    [[nodiscard]] auto as_objupvalue() const -> ObjUpvalue *;
    [[nodiscard]] auto as_objfunction() const -> ObjFunction *;
    [[nodiscard]] auto as_objclosure() const -> ObjClosure *;
    [[nodiscard]] auto as_objnative() const -> ObjNative *;
    [[nodiscard]] auto as_objclass() const -> ObjClass *;
    [[nodiscard]] auto as_objinstance() const -> ObjInstance *;
    [[nodiscard]] auto as_objboundmethod() const -> ObjBoundMethod *;

    [[nodiscard]] auto as_string() const -> const std::string &;
    [[nodiscard]] auto as_native() const -> NativeFn;

    // Queries

    [[nodiscard]] auto is(ValueType type) const -> bool { return m_type == type; }

    [[nodiscard]] auto is_boolean() const -> bool { return is(ValueType::Boolean); }
    [[nodiscard]] auto is_nil() const -> bool { return is(ValueType::Nil); }
    [[nodiscard]] auto is_number() const -> bool { return is(ValueType::Number); }
    [[nodiscard]] auto is_obj() const -> bool { return is(ValueType::Obj); }

    [[nodiscard]] auto is_string() const -> bool;
    [[nodiscard]] auto is_upvalue() const -> bool;
    [[nodiscard]] auto is_function() const -> bool;
    [[nodiscard]] auto is_closure() const -> bool;
    [[nodiscard]] auto is_native() const -> bool;
    [[nodiscard]] auto is_class() const -> bool;
    [[nodiscard]] auto is_instance() const -> bool;
    [[nodiscard]] auto is_bound_method() const -> bool;

    auto operator==(const Value & other) const -> bool;

private:
    ValueType m_type;
    ValueData m_as;
};

} // namespace cpplox

template <>
struct std::formatter<cpplox::Value::ValueType> : cpplox::EnumFormatter<cpplox::Value::ValueType>
{
};

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    auto format(const cpplox::Value & value, std::format_context & ctx) const
            -> std::format_context::iterator;
};
