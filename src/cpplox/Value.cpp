module cpplox;

import std;

import :Object;
import :Value;

namespace cpplox {

namespace {

template <ObjType type> auto value_is_obj_type(Value value) -> bool
{
    return value.is_obj() && value.as_obj()->get_type() == type;
}

} // namespace

auto Value::string(std::string data) -> Value
{
    return {ValueType::Obj, {.obj = ObjString::create(std::move(data))}};
}

auto Value::upvalue(Value * location) -> Value
{
    return {ValueType::Obj, {.obj = ObjUpvalue::create(location)}};
}

auto Value::function(std::string name) -> Value
{
    return {ValueType::Obj, {.obj = ObjFunction::create(std::move(name))}};
}

auto Value::closure(ObjFunction * function) -> Value
{
    return {ValueType::Obj, {.obj = ObjClosure::create(function)}};
}

auto Value::native(Value::NativeFn callable) -> Value
{
    return {ValueType::Obj, {.obj = ObjNative::create(callable)}};
}

auto Value::cls(ObjString * name) -> Value
{
    return {ValueType::Obj, {.obj = ObjClass::create(name)}};
}

auto Value::instance(ObjClass * cls) -> Value
{
    return {ValueType::Obj, {.obj = ObjInstance::create(cls)}};
}

auto Value::bound_method(Value receiver, ObjClosure * method) -> Value
{
    return {ValueType::Obj, {.obj = ObjBoundMethod::create(receiver, method)}};
}

auto Value::is_string() const -> bool { return value_is_obj_type<ObjType::String>(*this); }

auto Value::is_upvalue() const -> bool { return value_is_obj_type<ObjType::Upvalue>(*this); }

auto Value::is_function() const -> bool { return value_is_obj_type<ObjType::Function>(*this); }

auto Value::is_closure() const -> bool { return value_is_obj_type<ObjType::Closure>(*this); }

auto Value::is_native() const -> bool { return value_is_obj_type<ObjType::Native>(*this); }

auto Value::is_bound_method() const -> bool
{
    return value_is_obj_type<ObjType::BoundMethod>(*this);
}

auto Value::is_class() const -> bool { return value_is_obj_type<ObjType::Class>(*this); }

auto Value::is_instance() const -> bool { return value_is_obj_type<ObjType::Instance>(*this); }

auto Value::as_objstring() const -> ObjString * { return dynamic_cast<ObjString *>(as_obj()); }

auto Value::as_objupvalue() const -> ObjUpvalue * { return dynamic_cast<ObjUpvalue *>(as_obj()); }

auto Value::as_objfunction() const -> ObjFunction *
{
    return dynamic_cast<ObjFunction *>(as_obj());
}

auto Value::as_objclosure() const -> ObjClosure * { return dynamic_cast<ObjClosure *>(as_obj()); }

auto Value::as_objnative() const -> ObjNative * { return dynamic_cast<ObjNative *>(as_obj()); }

auto Value::as_objclass() const -> ObjClass * { return dynamic_cast<ObjClass *>(as_obj()); }

auto Value::as_objinstance() const -> ObjInstance *
{
    return dynamic_cast<ObjInstance *>(as_obj());
}

auto Value::as_objboundmethod() const -> ObjBoundMethod *
{
    return dynamic_cast<ObjBoundMethod *>(as_obj());
}

auto Value::as_string() const -> const std::string & { return as_objstring()->data(); }

auto Value::as_native() const -> Value::NativeFn { return as_objnative()->get_callable(); }

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

auto std::formatter<cpplox::Value>::format(
        const cpplox::Value & value, std::format_context & ctx
) const -> std::format_context::iterator
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
        case cpplox::ObjType::Class:
            return std::format_to(ctx.out(), "<class {}>", value.as_objclass()->get_name()->data());
        case cpplox::ObjType::Instance:
            return std::format_to(
                    ctx.out(),
                    "<class {} instance>",
                    value.as_objinstance()->get_class()->get_name()->data()
            );
        case cpplox::ObjType::BoundMethod: {
            auto name = value.as_objboundmethod()->get_method()->get_function()->get_name();
            if (name.empty()) {
                return std::format_to(ctx.out(), "<script>");
            }
            return std::format_to(ctx.out(), "<fn {}>", name);
        }
        }
    }
}
