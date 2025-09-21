export module cpplox:Object;

import std;

export import :ObjectFwd;

import :Chunk;
import :EnumFormatter;
import :ObjType;
import :Value;

namespace cpplox {

export class Obj
{
public:
    explicit Obj(ObjType type)
        : m_type(type)
    {
    }

    virtual ~Obj() = default;

    [[nodiscard]] constexpr auto get_type() const -> ObjType { return m_type; }

    // TODO: Should probably be virtual instead of having out-of-line mark_object()
    constexpr auto mark() -> void { m_marked = true; }
    constexpr auto clear_mark() -> void { m_marked = false; }
    [[nodiscard]] constexpr auto is_marked() const -> bool { return m_marked; }

private:
    ObjType m_type;
    bool m_marked = false;
};

export class ObjString : public Obj
{
public:
    static auto create(std::string data) -> ObjString *;

public:
    explicit ObjString(std::string data)
        : Obj(ObjType::String)
        , m_data(std::move(data))
    {
    }

    [[nodiscard]] constexpr auto data() const -> const std::string & { return m_data; }

private:
    // TODO: Can we do a flexible array member for this to avoid double indirection?
    // See: ch. 19 challenges, https://en.wikipedia.org/wiki/Flexible_array_member
    std::string m_data;
};

export class ObjUpvalue : public Obj
{
public:
    static auto create(Value * location) -> ObjUpvalue *;

public:
    explicit ObjUpvalue(Value * location)
        : Obj(ObjType::Upvalue)
        , m_location(location)
        , m_closed(Value::nil())
    {
    }

    [[nodiscard]] constexpr auto location() const -> Value * { return m_location; }

    constexpr auto set_next(ObjUpvalue * next) -> void { m_next = next; }
    [[nodiscard]] constexpr auto next() const -> ObjUpvalue * { return m_next; }

    constexpr auto close() -> void
    {
        m_closed = *m_location;
        m_location = &m_closed;
    }

private:
    Value * m_location;
    Value m_closed;
    ObjUpvalue * m_next = nullptr; // intrusive list
};

export class ObjFunction : public Obj
{
public:
    static auto create(std::string name) -> ObjFunction *;

public:
    explicit ObjFunction(std::string name)
        : Obj(ObjType::Function)
        , m_name(std::move(name))
    {
    }

    [[nodiscard]] constexpr auto get_name() const -> std::string_view { return m_name; }

    template <class Self> [[nodiscard]] auto get_chunk(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_chunk;
    }

    template <class Self> [[nodiscard]] auto arity(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_arity;
    }

    template <class Self> [[nodiscard]] auto upvalue_count(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_upvalue_count;
    }

private:
    std::size_t m_arity = 0;
    std::size_t m_upvalue_count = 0;
    Chunk m_chunk;
    std::string m_name;
};

class ObjNative : public Obj
{
public:
    static auto create(Value::NativeFn callable) -> ObjNative *;

public:
    explicit ObjNative(Value::NativeFn callable)
        : Obj(ObjType::Native)
        , m_callable(callable)
    {
    }

    [[nodiscard]] constexpr auto get_callable() const -> Value::NativeFn { return m_callable; }

private:
    Value::NativeFn m_callable;
};

export class ObjClosure : public Obj
{
public:
    static auto create(ObjFunction * function) -> ObjClosure *;

public:
    explicit ObjClosure(ObjFunction * function)
        : Obj(ObjType::Closure)
        , m_function(function)
    {
    }

    [[nodiscard]] constexpr auto get_function() const -> ObjFunction * { return m_function; }

    constexpr auto add_upvalue(ObjUpvalue * upvalue) -> void { m_upvalues.push_back(upvalue); }

    [[nodiscard]] constexpr auto upvalues() const -> std::span<ObjUpvalue * const>
    {
        return m_upvalues;
    }

private:
    ObjFunction * m_function;
    std::vector<ObjUpvalue *> m_upvalues;
};

export class ObjClass : public Obj
{
public:
    static auto create(ObjString * name) -> ObjClass *;

public:
    explicit ObjClass(ObjString * name)
        : Obj(ObjType::Class)
        , m_name(name)
    {
    }

    [[nodiscard]] constexpr auto get_name() const -> ObjString * { return m_name; }

    [[nodiscard]] constexpr auto get_method(const std::string & name) -> std::optional<Value>
    {
        auto it = m_methods.find(name);
        if (it != m_methods.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    constexpr auto add_method(const std::string & name, Value method) -> void
    {
        auto it = m_methods.find(name);
        if (it != m_methods.end()) {
            it->second = method;
        }
        else {
            m_methods.emplace(name, method);
        }
    }

    [[nodiscard]] constexpr auto all_methods() const -> std::unordered_map<std::string, Value>
    {
        return m_methods;
    }

private:
    ObjString * m_name; // TODO: somehow use string_view into source code instead?
    std::unordered_map<std::string, Value> m_methods;
};

export class ObjInstance : public Obj
{
public:
    static auto create(ObjClass * cls) -> ObjInstance *;

public:
    explicit ObjInstance(ObjClass * cls)
        : Obj(ObjType::Instance)
        , m_class(cls)
    {
    }

    [[nodiscard]] constexpr auto get_class() const -> ObjClass * { return m_class; }

    [[nodiscard]] constexpr auto get_field(const std::string & name) const -> std::optional<Value>
    {
        auto it = m_fields.find(name);
        if (it != m_fields.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    constexpr auto set_field(const std::string & name, Value value) -> void
    {
        auto it = m_fields.find(name);
        if (it != m_fields.end()) {
            it->second = value;
        }
        else {
            m_fields.emplace(name, value);
        }
    }

    [[nodiscard]] constexpr auto all_fields() const -> std::unordered_map<std::string, Value>
    {
        return m_fields;
    }

private:
    ObjClass * m_class;
    std::unordered_map<std::string, Value> m_fields;
};

export class ObjBoundMethod : public Obj
{
public:
    static auto create(Value receiver, ObjClosure * method) -> ObjBoundMethod *;

public:
    ObjBoundMethod(Value receiver, ObjClosure * method)
        : Obj(ObjType::BoundMethod)
        , m_receiver(receiver)
        , m_method(method)
    {
    }

    [[nodiscard]] constexpr auto get_receiver() const -> Value { return m_receiver; }

    [[nodiscard]] constexpr auto get_method() const -> ObjClosure * { return m_method; }

private:
    Value m_receiver;
    ObjClosure * m_method;
};

} // namespace cpplox
