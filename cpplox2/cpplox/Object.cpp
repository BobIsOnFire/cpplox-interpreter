export module cpplox2:Object;

import :Value;
import :VirtualMachine;

import std;

namespace cpplox {

export enum class ObjType : std::uint8_t {
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

// TODO: Can we do a flexible array member for this to avoid double indirection?
// See: ch. 19 challenges, https://en.wikipedia.org/wiki/Flexible_array_member
export class ObjString
    : public Obj
    , public std::string
{
public:
    explicit ObjString(std::string data)
        : Obj(ObjType::String)
        , std::string(std::move(data))
    {
    }
};

namespace {

template <ObjType type> auto value_is_obj_type(Value value) -> bool
{
    return value.is_obj() && value.as_obj()->get_type() == type;
}

} // namespace

// TODO: feels complicated that there are out-of-class definitions in a
// completely separate module
constexpr auto Value::string(std::string data) -> Value
{
    Obj * obj = new ObjString(std::move(data));
    g_vm.objects.push_back(obj);
    return {ValueType::Obj, {.obj = obj}};
}

constexpr auto Value::is_string() const -> bool
{
    return value_is_obj_type<ObjType::String>(*this);
}

constexpr auto Value::as_objstring() const -> ObjString *
{
    return dynamic_cast<ObjString *>(as_obj());
}

constexpr auto Value::as_string() const -> std::string &
{
    return *static_cast<std::string *>(as_objstring());
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
        }
    }
}

} // namespace cpplox
