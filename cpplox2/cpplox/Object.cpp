export module cpplox2:Object;

import :Compiler;
import :Chunk;
import :EnumFormatter;
import :Value;
import :VirtualMachine;

import std;

import magic_enum;

namespace cpplox {

export enum class ObjType : std::uint8_t {
    Closure,
    Function,
    Native,
    String,
    Upvalue,
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

    // TODO: Should probably be virtual instead of having out-of-line mark_object()
    constexpr auto mark() -> void { m_marked = true; }
    constexpr auto clear_mark() -> void { m_marked = false; }
    [[nodiscard]] constexpr auto is_marked() const -> bool { return m_marked; }

private:
    ObjType m_type;
    bool m_marked = false;
};

namespace {
constexpr const bool DEBUG_RUN_GC_EVERY_TIME = false;
constexpr const bool DEBUG_LOG_GC = true;
constexpr const std::size_t GC_HEAP_GROW_FACTOR = 2;

auto object_size(ObjType type) -> std::size_t;
auto collect_garbage() -> void;

} // namespace

export template <std::derived_from<Obj> T, typename... Args>
auto make_object(Args &&... args) -> T *
{
    if constexpr (DEBUG_RUN_GC_EVERY_TIME) {
        collect_garbage();
    }

    if (g_vm.bytes_allocated >= g_vm.next_gc) {
        collect_garbage();
    }

    T * obj = new T(std::forward<Args>(args)...); // NOLINT(cppcoreguidelines-owning-memory)

    if constexpr (DEBUG_LOG_GC) {
        std::println(
                "Created {} at {}", magic_enum::enum_name(obj->get_type()), static_cast<void *>(obj)
        );
    }

    g_vm.objects.push_back(obj);

    g_vm.bytes_allocated += sizeof(T);

    return obj;
}

export auto release_object(Obj * obj) -> void
{
    auto type = obj->get_type();

    delete obj; // NOLINT(cppcoreguidelines-owning-memory)

    g_vm.bytes_allocated -= object_size(type);

    if constexpr (DEBUG_LOG_GC) {
        std::println("Released {} at {}", magic_enum::enum_name(type), static_cast<void *>(obj));
    }
}

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
        auto * obj = make_object<ObjString>(std::move(data));
        return obj;
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
    explicit ObjUpvalue(Value * location)
        : Obj(ObjType::Upvalue)
        , m_location(location)
        , m_closed(Value::nil())
    {
    }

    constexpr static auto create(Value * location) -> ObjUpvalue *
    {
        auto * obj = make_object<ObjUpvalue>(location);
        return obj;
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
        auto * obj = make_object<ObjFunction>(std::move(name));
        return obj;
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

// very cool clang, you don't see usage within deducing this template and now i'm sad
static_assert(requires(const ObjFunction & obj) {
    obj.arity();
    obj.upvalue_count();
});

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
        auto * obj = make_object<ObjNative>(callable);
        return obj;
    }

    [[nodiscard]] constexpr auto get_callable() const -> Value::NativeFn { return m_callable; }

private:
    Value::NativeFn m_callable;
};

export class ObjClosure : public Obj
{
public:
    explicit ObjClosure(ObjFunction * function)
        : Obj(ObjType::Closure)
        , m_function(function)
    {
    }

    constexpr static auto create(ObjFunction * function) -> ObjClosure *
    {
        auto * obj = make_object<ObjClosure>(function);
        return obj;
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

// TODO: feels complicated that there are out-of-class definitions in a
// completely separate module
constexpr auto Value::string(std::string data) -> Value
{
    return {ValueType::Obj, {.obj = ObjString::create(std::move(data))}};
}

constexpr auto Value::upvalue(Value * location) -> Value
{
    return {ValueType::Obj, {.obj = ObjUpvalue::create(location)}};
}

constexpr auto Value::function(std::string name) -> Value
{
    return {ValueType::Obj, {.obj = ObjFunction::create(std::move(name))}};
}

constexpr auto Value::closure(ObjFunction * function) -> Value
{
    return {ValueType::Obj, {.obj = ObjClosure::create(function)}};
}

constexpr auto Value::native(Value::NativeFn callable) -> Value
{
    return {ValueType::Obj, {.obj = ObjNative::create(callable)}};
}

constexpr auto Value::is_string() const -> bool
{
    return value_is_obj_type<ObjType::String>(*this);
}

constexpr auto Value::is_upvalue() const -> bool
{
    return value_is_obj_type<ObjType::Upvalue>(*this);
}

constexpr auto Value::is_function() const -> bool
{
    return value_is_obj_type<ObjType::Function>(*this);
}

constexpr auto Value::is_closure() const -> bool
{
    return value_is_obj_type<ObjType::Closure>(*this);
}

constexpr auto Value::is_native() const -> bool
{
    return value_is_obj_type<ObjType::Native>(*this);
}

constexpr auto Value::as_objstring() const -> ObjString *
{
    return dynamic_cast<ObjString *>(as_obj());
}

constexpr auto Value::as_objupvalue() const -> ObjUpvalue *
{
    return dynamic_cast<ObjUpvalue *>(as_obj());
}

constexpr auto Value::as_objfunction() const -> ObjFunction *
{
    return dynamic_cast<ObjFunction *>(as_obj());
}

constexpr auto Value::as_objclosure() const -> ObjClosure *
{
    return dynamic_cast<ObjClosure *>(as_obj());
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

template <> struct std::formatter<cpplox::ValueType> : cpplox::EnumFormatter<cpplox::ValueType>
{
};

template <> struct std::formatter<cpplox::ObjType> : cpplox::EnumFormatter<cpplox::ObjType>
{
};

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    auto format(const cpplox::Value & value, std::format_context & ctx) const
    {
        switch (value.get_type()) {
        case cpplox::ValueType::Boolean: return std::format_to(ctx.out(), "{}", value.as_boolean());
        case cpplox::ValueType::Nil: return std::format_to(ctx.out(), "nil");
        case cpplox::ValueType::Number: return std::format_to(ctx.out(), "{}", value.as_number());
        case cpplox::ValueType::Obj:
            switch (value.as_obj()->get_type()) {
            case cpplox::ObjType::String:
                return std::formatter<std::string_view>::format(value.as_string(), ctx);
            case cpplox::ObjType::Upvalue: return std::format_to(ctx.out(), "upvalue");
            case cpplox::ObjType::Function: {
                auto name = value.as_objfunction()->get_name();
                if (name.empty()) {
                    return std::format_to(ctx.out(), "<script>");
                }
                return std::format_to(ctx.out(), "<fn {}>", name);
            }
            case cpplox::ObjType::Closure: {
                auto name = value.as_objclosure()->get_function()->get_name();
                if (name.empty()) {
                    return std::format_to(ctx.out(), "<script>");
                }
                return std::format_to(ctx.out(), "<fn {}>", name);
            }
            case cpplox::ObjType::Native: return std::format_to(ctx.out(), "<native fn>");
            }
        }
    }
};

namespace cpplox { namespace {

auto object_size(ObjType type) -> std::size_t
{
    switch (type) {
    case ObjType::Closure: return sizeof(ObjClosure);
    case ObjType::Function: return sizeof(ObjFunction);
    case ObjType::Native: return sizeof(ObjNative);
    case ObjType::String: return sizeof(ObjString);
    case ObjType::Upvalue: return sizeof(ObjUpvalue);
    }
}

auto mark_object(Obj * obj) -> void
{
    if (obj == nullptr) {
        return;
    }
    if (obj->is_marked()) {
        return;
    }
    if constexpr (DEBUG_LOG_GC) {
        std::println(
                "Mark {} at {} ({})",
                magic_enum::enum_name(obj->get_type()),
                static_cast<void *>(obj),
                Value::obj(obj)
        );
    }
    obj->mark();
    g_vm.gray_objects.insert(obj);
}

auto mark_value(const Value & value) -> void
{
    if (value.is_obj()) {
        mark_object(value.as_obj());
    }
}

auto blacken_object(Obj * obj) -> void
{
    if constexpr (DEBUG_LOG_GC) {
        std::println(
                "Blacken {} at {} ({})",
                magic_enum::enum_name(obj->get_type()),
                static_cast<void *>(obj),
                Value::obj(obj)
        );
    }

    // TODO: definitely should be a virtual method in Obj classes
    switch (obj->get_type()) {
    case ObjType::Closure: {
        auto * closure = dynamic_cast<ObjClosure *>(obj);
        mark_object(closure->get_function());
        for (const auto & value : closure->upvalues()) {
            mark_object(value);
        }
        break;
    }
    case ObjType::Function: {
        auto * function = dynamic_cast<ObjFunction *>(obj);
        for (const auto & value : function->get_chunk().constants) {
            mark_value(value);
        }
        break;
    }
    case ObjType::Native:
    case ObjType::String: break;
    case ObjType::Upvalue: mark_value(*dynamic_cast<ObjUpvalue *>(obj)->location()); break;
    }
}

auto mark_compiler_roots() -> void
{
    Compiler * compiler = g_current_compiler;
    while (compiler != nullptr) {
        mark_object(compiler->function);
        compiler = compiler->enclosing;
    }
}

auto mark_roots() -> void
{
    for (const auto & value : g_vm.stack) {
        mark_value(value);
    }

    for (const auto & frame : g_vm.frames) {
        mark_object(frame.closure);
    }

    for (ObjUpvalue * upvalue = g_vm.open_upvalues; upvalue != nullptr; upvalue = upvalue->next()) {
        mark_object(upvalue);
    }

    for (const auto & [_, value] : g_vm.globals) {
        mark_value(value);
    }

    mark_compiler_roots();
}

auto trace_references() -> void
{
    while (!g_vm.gray_objects.empty()) {
        auto it = g_vm.gray_objects.begin();
        Obj * obj = *it;
        g_vm.gray_objects.erase(it);

        blacken_object(obj);
    }
}

auto sweep() -> void
{
    std::vector<Obj *> new_objects;
    for (auto * obj : g_vm.objects) {
        if (obj->is_marked()) {
            obj->clear_mark();
            new_objects.push_back(obj);
        }
        else {
            release_object(obj);
        }
    }
    g_vm.objects = std::move(new_objects);
}

auto collect_garbage() -> void
{
    if constexpr (DEBUG_LOG_GC) {
        std::println("-- gc begin");
    }

    std::size_t before = g_vm.bytes_allocated;

    mark_roots();
    trace_references();
    sweep();

    g_vm.next_gc = g_vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

    if constexpr (DEBUG_LOG_GC) {
        std::println("-- gc end");
        std::println(
                "   collected {} bytes (from {} to {}), next gc at {}",
                before - g_vm.bytes_allocated,
                before,
                g_vm.bytes_allocated,
                g_vm.next_gc
        );
    }
}

}} // namespace cpplox
