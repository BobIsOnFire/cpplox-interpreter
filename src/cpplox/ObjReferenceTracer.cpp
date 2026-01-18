module;

#include <cassert>

module cpplox;

import :Object;
import :ObjReferenceTracer;

namespace cpplox {

auto ObjReferenceTracer::trace(Obj * obj) -> void
{
    // All previously added objects must be already processed
    assert(m_to_trace.empty());
    mark(obj);
    while (!m_to_trace.empty()) {
        Obj * next = m_to_trace.top();
        m_to_trace.pop();
        gc_log(next, "Blacken");
        trace_references(next);
    }
}

auto ObjReferenceTracer::trace(Value value) -> void
{
    if (value.is_obj()) {
        trace(value.as_obj());
    }
}

auto ObjReferenceTracer::is_referenced(Obj * obj) const -> bool
{
    return m_referenced.contains(obj);
}

auto ObjReferenceTracer::gc_log(Obj * obj, std::string_view action) const -> void
{
    if (m_settings.debug_log_gc) [[unlikely]] {
        std::println(
                std::cerr,
                "{} {} at {} ({})",
                action,
                magic_enum::enum_name(obj->get_type()),
                static_cast<void *>(obj),
                Value::obj(obj)
        );
    }
}

auto ObjReferenceTracer::mark(Obj * obj) -> void
{
    if (obj == nullptr) {
        return;
    }
    if (m_referenced.contains(obj)) {
        return;
    }
    gc_log(obj, "Mark");
    m_to_trace.push(obj);
    m_referenced.insert(obj);
}

auto ObjReferenceTracer::mark(const Value & value) -> void
{
    if (value.is_obj()) {
        mark(value.as_obj());
    }
}

auto ObjReferenceTracer::trace_references(Obj * obj) -> void
{
    switch (obj->get_type()) {
    case Obj::ObjType::Closure: {
        auto * closure = dynamic_cast<ObjClosure *>(obj);
        mark(closure->get_function());
        for (const auto & value : closure->upvalues()) {
            mark(value);
        }
        break;
    }
    case Obj::ObjType::Function: {
        auto * function = dynamic_cast<ObjFunction *>(obj);
        for (const auto & value : function->get_chunk().constants()) {
            mark(value);
        }
        break;
    }
    case Obj::ObjType::Native:
    case Obj::ObjType::String: break;
    case Obj::ObjType::Upvalue: mark(*dynamic_cast<ObjUpvalue *>(obj)->location()); break;
    case Obj::ObjType::Class: {
        auto * cls = dynamic_cast<ObjClass *>(obj);
        mark(cls->get_name());
        for (const auto & [_, value] : cls->all_methods()) {
            mark(value);
        }
        break;
    }
    case Obj::ObjType::Instance: {
        auto * instance = dynamic_cast<ObjInstance *>(obj);
        mark(instance->get_class());
        for (const auto & [_, value] : instance->all_fields()) {
            mark(value);
        }
        break;
    }
    case Obj::ObjType::BoundMethod: {
        auto * bound_method = dynamic_cast<ObjBoundMethod *>(obj);
        mark(bound_method->get_receiver());
        mark(bound_method->get_method());
        break;
    }
    }
}

} // namespace cpplox
