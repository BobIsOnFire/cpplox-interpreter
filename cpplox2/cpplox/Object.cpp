export module cpplox2:Object;

import :Chunk;
import :Value;
import :VirtualMachine;

import std;

namespace cpplox {

export enum class ObjType : std::uint8_t {
    Function,
    Native,
    String,
};

export class Obj
{
public:
    explicit Obj(ObjType type)
        : m_type(type)
    {
    }

    virtual ~Obj() = default;

    [[nodiscard]] constexpr auto get_type() const -> ObjType { return m_type; }

private:
    ObjType m_type;
};

export class ObjString : public Obj
{
public:
    explicit ObjString(std::string data)
        : Obj(ObjType::String)
        , m_data(std::move(data))
    {
    }

    constexpr static auto create(std::string data) -> ObjString *
    {
        auto * obj = new ObjString(std::move(data));
        g_vm.objects.push_back(obj);
        return obj;
    }

    [[nodiscard]] constexpr auto data() const -> const std::string & { return m_data; }

private:
    // TODO: Can we do a flexible array member for this to avoid double indirection?
    // See: ch. 19 challenges, https://en.wikipedia.org/wiki/Flexible_array_member
    std::string m_data;
};

namespace {

template <ObjType type> auto value_is_obj_type(Value value) -> bool
{
    return value.is_obj() && value.as_obj()->get_type() == type;
}

} // namespace

export class ObjFunction : public Obj
{
public:
    explicit ObjFunction(std::string name)
        : Obj(ObjType::Function)
        , m_name(std::move(name))
    {
    }

    constexpr static auto create(std::string name) -> ObjFunction *
    {
        auto * obj = new ObjFunction(std::move(name));
        g_vm.objects.push_back(obj);
        return obj;
    }

    [[nodiscard]] constexpr auto get_name() const -> std::string_view { return m_name; }

    template <class Self> [[nodiscard]] auto get_chunk(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_chunk;
    }

    template <class Self> [[nodiscard]] auto get_arity(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_arity;
    }

private:
    std::size_t m_arity = 0;
    Chunk m_chunk;
    std::string m_name;
};

// very cool clang, you don't see usage within deducing this template and now i'm sad
static_assert(requires(const ObjFunction & obj) { obj.get_arity(); });

class ObjNative : public Obj
{
public:
    explicit ObjNative(Value::NativeFn callable)
        : Obj(ObjType::Native)
        , m_callable(callable)
    {
    }

    constexpr static auto create(Value::NativeFn callable) -> ObjNative *
    {
        auto * obj = new ObjNative(callable);
        g_vm.objects.push_back(obj);
        return obj;
    }

    [[nodiscard]] constexpr auto get_callable() const -> Value::NativeFn { return m_callable; }

private:
    Value::NativeFn m_callable;
};

// TODO: feels complicated that there are out-of-class definitions in a
// completely separate module
constexpr auto Value::string(std::string data) -> Value
{
    return {ValueType::Obj, {.obj = ObjString::create(std::move(data))}};
}

constexpr auto Value::function(std::string name) -> Value
{
    return {ValueType::Obj, {.obj = ObjFunction::create(std::move(name))}};
}

constexpr auto Value::native(Value::NativeFn callable) -> Value
{
    return {ValueType::Obj, {.obj = ObjNative::create(callable)}};
}

constexpr auto Value::is_string() const -> bool
{
    return value_is_obj_type<ObjType::String>(*this);
}

constexpr auto Value::is_function() const -> bool
{
    return value_is_obj_type<ObjType::Function>(*this);
}

constexpr auto Value::is_native() const -> bool
{
    return value_is_obj_type<ObjType::Native>(*this);
}

constexpr auto Value::as_objstring() const -> ObjString *
{
    return dynamic_cast<ObjString *>(as_obj());
}

constexpr auto Value::as_objfunction() const -> ObjFunction *
{
    return dynamic_cast<ObjFunction *>(as_obj());
}

constexpr auto Value::as_objnative() const -> ObjNative *
{
    return dynamic_cast<ObjNative *>(as_obj());
}

constexpr auto Value::as_string() const -> const std::string & { return as_objstring()->data(); }

constexpr auto Value::as_native() const -> Value::NativeFn
{
    return as_objnative()->get_callable();
}

auto Value::operator==(const Value & other) const -> bool
{
    if (m_type != other.m_type) {
        return false;
    }

    switch (m_type) {
    case ValueType::Boolean: return as_boolean() == other.as_boolean();
    case ValueType::Nil: return true;
    case ValueType::Number: return as_number() == other.as_number();
    case ValueType::Obj:
        switch (as_obj()->get_type()) {
        case ObjType::String: return as_string() == other.as_string();
        default: return as_obj() == other.as_obj();
        }
    }
}

} // namespace cpplox
