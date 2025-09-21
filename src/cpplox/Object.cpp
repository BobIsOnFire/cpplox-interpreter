module cpplox;

import :Compiler;
import :Chunk;
import :EnumFormatter;
import :Object;
import :Value;
import :VirtualMachine;

import std;

import magic_enum;

namespace cpplox {

namespace {
constexpr const bool DEBUG_RUN_GC_EVERY_TIME = false;
constexpr const bool DEBUG_LOG_GC = false;
constexpr const std::size_t GC_HEAP_GROW_FACTOR = 2;

auto object_size(Obj::ObjType type) -> std::size_t;
auto collect_garbage() -> void;

template <std::derived_from<Obj> T, typename... Args> auto save_object(T * obj) -> T *
{
    if constexpr (DEBUG_RUN_GC_EVERY_TIME) {
        collect_garbage();
    }

    if (g_vm.bytes_allocated >= g_vm.next_gc) {
        collect_garbage();
    }

    if constexpr (DEBUG_LOG_GC) {
        std::println(
                "Created {} at {}", magic_enum::enum_name(obj->get_type()), static_cast<void *>(obj)
        );
    }

    g_vm.objects.push_back(obj);
    g_vm.bytes_allocated += sizeof(T);

    return obj;
}

} // namespace

auto release_object(Obj * obj) -> void
{
    auto type = obj->get_type();

    delete obj; // NOLINT(cppcoreguidelines-owning-memory)

    g_vm.bytes_allocated -= object_size(type);

    if constexpr (DEBUG_LOG_GC) {
        std::println("Released {} at {}", magic_enum::enum_name(type), static_cast<void *>(obj));
    }
}

auto ObjString::create(std::string data) -> ObjString *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjString(std::move(data)));
}

auto ObjUpvalue::create(Value * location) -> ObjUpvalue *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjUpvalue(location));
}

auto ObjFunction::create(std::string name) -> ObjFunction *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjFunction(std::move(name)));
}

auto ObjNative::create(Value::NativeFn callable) -> ObjNative *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjNative(callable));
}

auto ObjClosure::create(ObjFunction * function) -> ObjClosure *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjClosure(function));
}

auto ObjClass::create(ObjString * name) -> ObjClass *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjClass(name));
}

auto ObjInstance::create(ObjClass * cls) -> ObjInstance *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjInstance(cls));
}

auto ObjBoundMethod::create(Value receiver, ObjClosure * method) -> ObjBoundMethod *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return save_object(new ObjBoundMethod(receiver, method));
}

} // namespace cpplox

namespace cpplox { namespace {

auto object_size(Obj::ObjType type) -> std::size_t
{
    switch (type) {
    case Obj::ObjType::Closure: return sizeof(ObjClosure);
    case Obj::ObjType::Function: return sizeof(ObjFunction);
    case Obj::ObjType::Native: return sizeof(ObjNative);
    case Obj::ObjType::String: return sizeof(ObjString);
    case Obj::ObjType::Upvalue: return sizeof(ObjUpvalue);
    case Obj::ObjType::Class: return sizeof(ObjClass);
    case Obj::ObjType::Instance: return sizeof(ObjInstance);
    case Obj::ObjType::BoundMethod: return sizeof(ObjBoundMethod);
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
    case Obj::ObjType::Closure: {
        auto * closure = dynamic_cast<ObjClosure *>(obj);
        mark_object(closure->get_function());
        for (const auto & value : closure->upvalues()) {
            mark_object(value);
        }
        break;
    }
    case Obj::ObjType::Function: {
        auto * function = dynamic_cast<ObjFunction *>(obj);
        for (const auto & value : function->get_chunk().constants) {
            mark_value(value);
        }
        break;
    }
    case Obj::ObjType::Native:
    case Obj::ObjType::String: break;
    case Obj::ObjType::Upvalue: mark_value(*dynamic_cast<ObjUpvalue *>(obj)->location()); break;
    case Obj::ObjType::Class: {
        auto * cls = dynamic_cast<ObjClass *>(obj);
        mark_object(cls->get_name());
        for (const auto & [_, value] : cls->all_methods()) {
            mark_value(value);
        }
        break;
    }
    case Obj::ObjType::Instance: {
        auto * instance = dynamic_cast<ObjInstance *>(obj);
        mark_object(instance->get_class());
        for (const auto & [_, value] : instance->all_fields()) {
            mark_value(value);
        }
        break;
    }
    case Obj::ObjType::BoundMethod: {
        auto * bound_method = dynamic_cast<ObjBoundMethod *>(obj);
        mark_value(bound_method->get_receiver());
        mark_object(bound_method->get_method());
        break;
    }
    }
}

// TODO: do not run GC when compiling and hide Compiler struct within module
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
