export module cpplox2:Value;

import std;

import magic_enum;

namespace cpplox {

export class Obj;
export class ObjString;
export class ObjUpvalue;
export class ObjFunction;
export class ObjClosure;
export class ObjNative;

export enum class ValueType : std::uint8_t {
    Boolean,
    Nil,
    Number,
    Obj,
};

// TODO: replace with std::variant?
export class Value
{
public:
    using NativeFn = Value (*)(std::span<const Value>);

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

    static constexpr auto boolean(bool value) -> Value
    {
        return {ValueType::Boolean, {.boolean = value}};
    }

    static constexpr auto nil() -> Value { return {ValueType::Nil, {.number = 0}}; }

    static constexpr auto number(double value) -> Value
    {
        return {ValueType::Number, {.number = value}};
    }

    // Do we need Obj/ObjString stuff in public members? Can we hide everything
    // behind our Obj * field?
    static constexpr auto obj(Obj * obj) -> Value { return {ValueType::Obj, {.obj = obj}}; }

    static constexpr auto string(std::string data) -> Value;
    static constexpr auto upvalue(Value *) -> Value;
    static constexpr auto function(std::string name) -> Value;
    static constexpr auto closure(ObjFunction *) -> Value;
    static constexpr auto native(NativeFn) -> Value;

    // Casts

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    [[nodiscard]] constexpr auto as_boolean() const -> bool { return m_as.boolean; }
    [[nodiscard]] constexpr auto as_number() const -> double { return m_as.number; }
    [[nodiscard]] constexpr auto as_obj() const -> Obj * { return m_as.obj; }
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    [[nodiscard]] constexpr auto as_objstring() const -> ObjString *;
    [[nodiscard]] constexpr auto as_objupvalue() const -> ObjUpvalue *;
    [[nodiscard]] constexpr auto as_objfunction() const -> ObjFunction *;
    [[nodiscard]] constexpr auto as_objclosure() const -> ObjClosure *;
    [[nodiscard]] constexpr auto as_objnative() const -> ObjNative *;

    [[nodiscard]] constexpr auto as_string() const -> const std::string &;
    [[nodiscard]] constexpr auto as_native() const -> NativeFn;

    // Queries

    [[nodiscard]] constexpr auto is(ValueType type) const -> bool { return m_type == type; }

    [[nodiscard]] constexpr auto is_boolean() const -> bool { return is(ValueType::Boolean); }
    [[nodiscard]] constexpr auto is_nil() const -> bool { return is(ValueType::Nil); }
    [[nodiscard]] constexpr auto is_number() const -> bool { return is(ValueType::Number); }
    [[nodiscard]] constexpr auto is_obj() const -> bool { return is(ValueType::Obj); }

    [[nodiscard]] constexpr auto is_string() const -> bool;
    [[nodiscard]] constexpr auto is_upvalue() const -> bool;
    [[nodiscard]] constexpr auto is_function() const -> bool;
    [[nodiscard]] constexpr auto is_closure() const -> bool;
    [[nodiscard]] constexpr auto is_native() const -> bool;

    auto operator==(const Value & other) const -> bool;

private:
    ValueType m_type;
    ValueData m_as;
};

} // namespace cpplox
