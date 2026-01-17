module;

#include <cassert>

module cpplox;

import std;

import :Compiler;
import :Debug;
import :Object;
import :OpCode;
import :VirtualMachine;

namespace cpplox {

struct CallFrame
{
    ObjClosure * closure;
    const Byte * ip;
    std::size_t stack_offset;
};

// FIXME: Should be inlined into constructor/desctructor, but should get rid of global object first
auto init_vm() -> void;
auto free_vm() -> void;

auto interpret(std::string_view source) -> InterpretResult;

struct VirtualMachine : IVirtualMachine
{
    VirtualMachine() {
        init_vm();
    }

    ~VirtualMachine() override {
        free_vm();
    }

    auto interpret(std::string_view source) -> InterpretResult override
    {
        return cpplox::interpret(source);
    }

    std::vector<CallFrame> frames;
    std::vector<Value> stack;
    std::vector<Obj *> objects;
    std::unordered_map<std::string, Value> globals;
    std::list<ObjUpvalue *> open_upvalues;

    std::unordered_set<Obj *> gray_objects; // gray-marked
    std::size_t bytes_allocated = 0;
    std::size_t next_gc = 1024UZ * 1024;
    bool gc_active = false;
};

// FIXME: get rid of singleton instance
VirtualMachine g_vm; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto make_vm() -> VirtualMachinePtr {
    // FIXME remove global object and replace with unique_ptr
    return &g_vm;
    // return std::make_unique<VirtualMachine>();
}

namespace {
constexpr const std::size_t FRAMES_MAX = 64;
constexpr const std::size_t STACK_MAX = 256;
const bool DEBUG_PRINT_CODE = std::getenv("LOX_DEBUG_PRINT_CODE") != nullptr;
const bool DEBUG_VM_EXECUTION = std::getenv("LOX_DEBUG_VM_EXECUTION") != nullptr;
const bool DEBUG_RUN_GC_EVERY_TIME = std::getenv("LOX_DEBUG_RUN_GC_EVERY_TIME") != nullptr;
const bool DEBUG_LOG_GC = std::getenv("LOX_DEBUG_LOG_GC") != nullptr;
constexpr const std::size_t GC_HEAP_GROW_FACTOR = 2;
} // namespace

namespace {

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

auto collect_garbage() -> void;

auto save_object(Obj * obj) -> void
{
    if (g_vm.gc_active) {
        if (DEBUG_RUN_GC_EVERY_TIME || g_vm.bytes_allocated >= g_vm.next_gc) {
            collect_garbage();
        }
    }

    if (DEBUG_LOG_GC) [[unlikely]] {
        std::println(
                std::cerr,
                "Created {} at {}",
                magic_enum::enum_name(obj->get_type()),
                static_cast<void *>(obj)
        );
    }

    g_vm.objects.push_back(obj);
    g_vm.bytes_allocated += object_size(obj->get_type());
}

auto release_object(Obj * obj) -> void
{
    auto type = obj->get_type();

    delete obj; // NOLINT(cppcoreguidelines-owning-memory)

    g_vm.bytes_allocated -= object_size(type);

    if (DEBUG_LOG_GC) [[unlikely]] {
        std::println(
                std::cerr,
                "Released {} at {}",
                magic_enum::enum_name(type),
                static_cast<void *>(obj)
        );
    }
}

template <std::derived_from<Obj> T, typename... Args>
    requires std::constructible_from<T, Args...>
auto new_object(Args &&... args) -> T *
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto * newobj = new T(std::forward<Args>(args)...);
    save_object(newobj);
    return newobj;
}

auto mark_object(Obj * obj) -> void
{
    if (obj == nullptr) {
        return;
    }
    if (obj->is_marked()) {
        return;
    }
    if (DEBUG_LOG_GC) [[unlikely]] {
        std::println(
                std::cerr,
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
    if (DEBUG_LOG_GC) [[unlikely]] {
        std::println(
                std::cerr,
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
        for (const auto & value : function->get_chunk().constants()) {
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

auto mark_roots() -> void
{
    for (const auto & value : g_vm.stack) {
        mark_value(value);
    }

    for (const auto & frame : g_vm.frames) {
        mark_object(frame.closure);
    }

    for (auto * upvalue : g_vm.open_upvalues) {
        mark_object(upvalue);
    }

    for (const auto & [_, value] : g_vm.globals) {
        mark_value(value);
    }
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
    if (DEBUG_LOG_GC) [[unlikely]] {
        std::println(std::cerr, "-- gc begin");
    }

    std::size_t before = g_vm.bytes_allocated;

    mark_roots();
    trace_references();
    sweep();

    g_vm.next_gc = g_vm.bytes_allocated * GC_HEAP_GROW_FACTOR;

    if (DEBUG_LOG_GC) [[unlikely]] {
        std::println(std::cerr, "-- gc end");
        std::println(
                std::cerr,
                "   collected {} bytes (from {} to {}), next gc at {}",
                before - g_vm.bytes_allocated,
                before,
                g_vm.bytes_allocated,
                g_vm.next_gc
        );
    }
}

auto current_frame() -> CallFrame & { return g_vm.frames.back(); }
auto current_chunk() -> Chunk & { return current_frame().closure->get_function()->get_chunk(); }

auto read_byte() -> Byte
{
    Byte b = *current_frame().ip;
    std::advance(current_frame().ip, 1);
    return b;
}

auto read_instruction() -> OpCode { return static_cast<OpCode>(read_byte()); }

auto read_constant() -> Value { return current_chunk().constants()[read_byte()]; }

auto read_double_byte() -> DoubleByte
{
    return static_cast<DoubleByte>(read_byte() << BYTE_DIGITS) | read_byte();
}

template <typename... Args> auto runtime_error(std::format_string<Args...> fmt, Args &&... args)
{
    std::print(std::cerr, "runtime error: ");
    std::println(std::cerr, fmt, std::forward<Args>(args)...);

    for (const auto & frame : std::ranges::reverse_view{g_vm.frames}) {
        const auto * function = frame.closure->get_function();

        const auto & chunk = function->get_chunk();
        auto chunk_offset = static_cast<std::size_t>(std::distance(chunk.code().data(), frame.ip));

        const auto & location = chunk.locations()[chunk_offset - 1];
        std::print(std::cerr, "  [{}:{}] in ", location.line, location.column);

        if (function->get_name().empty()) {
            std::println(std::cerr, "script");
        }
        else {
            std::println(std::cerr, "{}()", function->get_name());
        }
    }

    g_vm.stack.clear();
}

auto push_value(Value value) -> void
{
    assert(g_vm.stack.size() < STACK_MAX && "Value stack overflow");
    g_vm.stack.push_back(value);
}

auto pop_value() -> Value
{
    assert(g_vm.stack.size() > 0 && "Value stack empty");
    Value val = g_vm.stack.back();
    g_vm.stack.pop_back();
    return val;
}

auto peek_value(std::size_t distance = 0) -> Value
{
    assert(g_vm.stack.size() > distance && "Cannot peek, stack is not big enough");
    return g_vm.stack[g_vm.stack.size() - 1 - distance];
}

template <OpCode op> auto binary_op() -> InterpretResult
{
    if (!peek_value(0).is_number() || !peek_value(1).is_number()) {
        runtime_error("Operands must be numbers.");
        return InterpretResult::RuntimeError;
    }

    auto rhs = pop_value().as_number();
    auto lhs = pop_value().as_number();

    Value result = Value::nil();
    if constexpr (op == OpCode::Greater) {
        result = Value::boolean(lhs > rhs);
    }
    if constexpr (op == OpCode::Less) {
        result = Value::boolean(lhs < rhs);
    }
    if constexpr (op == OpCode::Add) {
        result = Value::number(lhs + rhs);
    }
    if constexpr (op == OpCode::Substract) {
        result = Value::number(lhs - rhs);
    }
    if constexpr (op == OpCode::Multiply) {
        result = Value::number(lhs * rhs);
    }
    if constexpr (op == OpCode::Divide) {
        result = Value::number(lhs / rhs);
    }

    push_value(result);
    return InterpretResult::Ok;
}

auto is_falsey(Value value) -> bool
{
    return value.is_nil() || (value.is_boolean() && !value.as_boolean());
}

// FIXME: should return InterpretResult or some other error type?
auto call(ObjClosure & closure, Byte arg_count) -> bool
{
    auto & function = *closure.get_function();
    if (arg_count != function.arity()) {
        runtime_error("Expected {} arguments but got {}.", function.arity(), arg_count);
        return false;
    }

    if (g_vm.frames.size() >= FRAMES_MAX) {
        runtime_error("Stack overflow.");
        return false;
    }

    auto slot_start = g_vm.stack.size() - arg_count - 1;

    g_vm.frames.push_back({
            .closure = &closure,
            .ip = function.get_chunk().code().data(),
            .stack_offset = slot_start,
    });

    return true;
}

auto call_value(Value callee, Byte arg_count) -> bool
{
    if (callee.is_bound_method()) {
        auto * bound = callee.as_objboundmethod();
        // place receiver before args on the stack to get 'this' resolved correctly to it
        g_vm.stack[g_vm.stack.size() - arg_count - 1] = bound->get_receiver();
        return call(*bound->get_method(), arg_count);
    }
    if (callee.is_class()) {
        auto * cls = callee.as_objclass();
        // place newly created instance before args on the stack to get 'this' resolved correctly to
        // it
        g_vm.stack[g_vm.stack.size() - arg_count - 1] = Value::obj(new_object<ObjInstance>(cls));

        auto init = cls->get_method("init");
        if (init.has_value()) {
            return call(*init->as_objclosure(), arg_count);
        }

        if (arg_count != 0) {
            runtime_error("Expected 0 arguments but got {}.", arg_count);
            return false;
        }

        return true;
    }
    if (callee.is_closure()) {
        return call(*callee.as_objclosure(), arg_count);
    }
    if (callee.is_native()) {
        Value::NativeFn callable = callee.as_native();
        std::size_t args_start = g_vm.stack.size() - arg_count;
        Value result = callable(std::span{g_vm.stack}.subspan(args_start, arg_count));

        for (Byte i = 0; i < arg_count; i++) {
            pop_value();
        }
        push_value(result);
        return true;
    }

    runtime_error("Can only call functions and classes.");
    return false;
}

auto invoke_from_class(ObjClass & cls, const std::string & name, Byte arg_count) -> bool
{
    auto method = cls.get_method(name);
    if (!method.has_value()) {
        runtime_error("Undefined property '{}'.", name);
        return false;
    }

    return call(*method->as_objclosure(), arg_count);
}

auto invoke(const std::string & name, Byte arg_count) -> bool
{
    Value receiver = peek_value(arg_count);

    if (!receiver.is_instance()) {
        runtime_error("Only instances have methods.");
        return false;
    }

    auto * instance = receiver.as_objinstance();

    auto field = instance->get_field(name);
    if (field.has_value()) {
        g_vm.stack[g_vm.stack.size() - arg_count - 1] = field.value();
        return call_value(field.value(), arg_count);
    }

    return invoke_from_class(*instance->get_class(), name, arg_count);
}

auto bind_method(ObjClass & cls, const std::string & name) -> bool
{
    auto method = cls.get_method(name);
    if (!method.has_value()) {
        runtime_error("Undefined property '{}'.", name);
        return false;
    }

    auto * bound = new_object<ObjBoundMethod>(peek_value(), method.value().as_objclosure());

    pop_value();
    push_value(Value::obj(bound));

    return true;
}

auto capture_upvalue(Value * local) -> ObjUpvalue *
{
    auto it = g_vm.open_upvalues.begin();
    while (it != g_vm.open_upvalues.end() && (*it)->location() > local) {
        it++;
    }

    if (it != g_vm.open_upvalues.end() && (*it)->location() == local) {
        return *it;
    }

    auto * created_upvalue = new_object<ObjUpvalue>(local);
    g_vm.open_upvalues.insert(it, created_upvalue);

    return created_upvalue;
}

auto close_upvalues(std::size_t last_offset) -> void
{
    Value * last_location = &g_vm.stack[last_offset];

    while (!g_vm.open_upvalues.empty() && g_vm.open_upvalues.front()->location() >= last_location) {
        auto * upvalue = g_vm.open_upvalues.front();
        upvalue->close();
        g_vm.open_upvalues.pop_front();
    }
}

auto define_method(const std::string & name) -> void
{
    Value method = peek_value();
    auto * cls = peek_value(1).as_objclass();

    cls->add_method(name, method);
    pop_value(); // once! leave class in place for consequent methods
}

auto define_native(std::string_view name, Value::NativeFn callable) -> void
{
    // pushing and popping some GC bullsheesh
    push_value(Value::obj(new_object<ObjNative>(callable)));
    g_vm.globals.emplace(name, g_vm.stack.back());
    pop_value();
}

} // namespace

// Yeah, sucks
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto run() -> InterpretResult
{
    using enum OpCode;

    for (;;) {
        InterpretResult op_result = InterpretResult::Ok;

        if (DEBUG_VM_EXECUTION) [[unlikely]] {
            print_stack(g_vm.stack);

            const auto * chunk_start = current_chunk().code().data();
            const auto offset
                    = static_cast<std::size_t>(std::distance(chunk_start, current_frame().ip));

            disassemble_instruction(current_chunk(), offset);
        }

        switch (read_instruction()) {
        // Values
        case Constant: {
            push_value(read_constant());
            break;
        }
        case Nil: push_value(Value::nil()); break;
        case True: push_value(Value::boolean(true)); break;
        case False: push_value(Value::boolean(false)); break;
        // Value manipulators
        case Pop: pop_value(); break;
        case DefineGlobal: {
            const std::string & name = read_constant().as_string();
            // FIXME: is there a way to get rid of copy on key insert? Key will surely live as long
            // as VM lives
            auto it = g_vm.globals.find(name);
            if (it != g_vm.globals.end()) {
                it->second = peek_value();
            }
            else {
                g_vm.globals.emplace(name, peek_value());
            }
            pop_value();
            break;
        }
        case GetGlobal: {
            const std::string & name = read_constant().as_string();
            auto it = g_vm.globals.find(name);
            if (it == g_vm.globals.end()) {
                runtime_error("Undefined variable '{}'.", name);
                return InterpretResult::RuntimeError;
            }
            push_value(it->second);
            break;
        }
        case GetLocal: {
            Byte slot = read_byte();
            push_value(g_vm.stack[current_frame().stack_offset + slot]);
            break;
        }
        case GetProperty: {
            if (!peek_value().is_instance()) {
                runtime_error("Only instances have properties.");
                return InterpretResult::RuntimeError;
            }

            auto * instance = peek_value().as_objinstance();
            const std::string & name = read_constant().as_string();

            auto property = instance->get_field(name);
            if (property.has_value()) {
                pop_value(); // instance object still on the stack
                push_value(property.value());
                break;
            }

            if (!bind_method(*instance->get_class(), name)) {
                return InterpretResult::RuntimeError;
            }
            break;
        }
        case GetSuper: {
            const std::string & name = read_constant().as_string();
            auto * super = pop_value().as_objclass();

            if (!bind_method(*super, name)) {
                return InterpretResult::RuntimeError;
            }

            break;
        }
        case GetUpvalue: {
            Byte slot = read_byte();
            push_value(*current_frame().closure->upvalues()[slot]->location());
            break;
        }
        case SetGlobal: {
            const std::string & name = read_constant().as_string();
            auto it = g_vm.globals.find(name);
            if (it == g_vm.globals.end()) {
                runtime_error("Undefined variable '{}'.", name);
                return InterpretResult::RuntimeError;
            }
            it->second = peek_value();
            break;
        }
        case SetLocal: {
            Byte slot = read_byte();
            g_vm.stack[current_frame().stack_offset + slot] = peek_value();
            break;
        }
        case SetProperty: {
            if (!peek_value(1).is_instance()) {
                runtime_error("Only instances have properties.");
                return InterpretResult::RuntimeError;
            }

            auto * instance = peek_value(1).as_objinstance();
            const std::string & name = read_constant().as_string();

            instance->set_field(name, peek_value());

            Value value = pop_value();
            pop_value(); // instance

            push_value(value);
            break;
        }
        case SetUpvalue: {
            Byte slot = read_byte();
            *current_frame().closure->upvalues()[slot]->location() = peek_value();
            break;
        }
        // Comparison ops
        case Equal: {
            Value rhs = pop_value();
            Value lhs = pop_value();
            push_value(Value::boolean(lhs == rhs));
            break;
        }
        case Greater: op_result = binary_op<Greater>(); break;
        case Less: op_result = binary_op<Less>(); break;
        // Binary ops
        case Add: {
            if (peek_value(0).is_string() && peek_value(1).is_string()) {
                const auto & rhs = peek_value(0).as_string();
                const auto & lhs = peek_value(1).as_string();
                auto value = Value::obj(new_object<ObjString>(lhs + rhs));
                pop_value();
                pop_value();
                push_value(value);
            }
            else if (peek_value(0).is_number() && peek_value(1).is_number()) {
                double rhs = pop_value().as_number();
                double lhs = pop_value().as_number();
                push_value(Value::number(lhs + rhs));
            }
            else {
                runtime_error("Operands must be two numbers or two strings.");
                return InterpretResult::RuntimeError;
            }
            break;
        }
        case Substract: op_result = binary_op<Substract>(); break;
        case Multiply: op_result = binary_op<Multiply>(); break;
        case Divide: op_result = binary_op<Divide>(); break;
        // Unary ops
        case Not: push_value(Value::boolean(is_falsey(pop_value()))); break;
        case Negate:
            if (!peek_value().is_number()) {
                runtime_error("Operand must be a number.");
                return InterpretResult::RuntimeError;
            }
            push_value(Value::number(-pop_value().as_number()));
            break;
        // Aux
        case Print: std::println("{}", pop_value()); break;
        case Jump: {
            DoubleByte offset = read_double_byte();
            std::advance(current_frame().ip, offset);
            break;
        }
        case JumpIfFalse: {
            DoubleByte offset = read_double_byte();
            if (is_falsey(peek_value())) {
                std::advance(current_frame().ip, offset);
            }
            break;
        }
        case Loop: {
            DoubleByte offset = read_double_byte();
            std::advance(current_frame().ip, -static_cast<std::ptrdiff_t>(offset));
            break;
        }
        case Call: {
            Byte arg_count = read_byte();
            if (!call_value(peek_value(arg_count), arg_count)) {
                return InterpretResult::RuntimeError;
            }
            break;
        }
        case Invoke: {
            const std::string & name = read_constant().as_string();
            Byte arg_count = read_byte();

            if (!invoke(name, arg_count)) {
                return InterpretResult::RuntimeError;
            }
            break;
        }
        case SuperInvoke: {
            const std::string & name = read_constant().as_string();
            Byte arg_count = read_byte();
            auto * super = pop_value().as_objclass();

            if (!invoke_from_class(*super, name, arg_count)) {
                return InterpretResult::RuntimeError;
            }
            break;
        }
        case Closure: {
            auto * function = read_constant().as_objfunction();
            auto * closure = new_object<ObjClosure>(function);
            push_value(Value::obj(closure));

            for (auto _ : std::views::iota(0UZ, function->upvalue_count())) {
                bool is_local = read_byte() == 1;
                Byte index = read_byte();
                if (is_local) {
                    closure->add_upvalue(
                            capture_upvalue(&g_vm.stack[current_frame().stack_offset + index])
                    );
                }
                else {
                    closure->add_upvalue(current_frame().closure->upvalues()[index]);
                }
            }
            break;
        }
        case CloseUpvalue: {
            close_upvalues(g_vm.stack.size() - 1);
            pop_value();
            break;
        }
        case Return: {
            Value result = pop_value();
            auto old_offset = g_vm.frames.back().stack_offset;
            close_upvalues(old_offset);

            g_vm.frames.pop_back();
            if (g_vm.frames.empty()) {
                pop_value();
                return InterpretResult::Ok;
            }

            // FIXME: yeah, dirt. Should be solved if we actually use array for stack
            while (g_vm.stack.size() != old_offset) {
                pop_value();
            }
            push_value(result);
            break;
        }
        case Class: {
            auto * name = read_constant().as_objstring();
            push_value(Value::obj(new_object<ObjClass>(name)));
            break;
        }
        case Inherit: {
            Value superclass_val = peek_value(1);
            if (!superclass_val.is_class()) {
                runtime_error("Superclass must be a class.");
                return InterpretResult::RuntimeError;
            }

            auto * superclass = superclass_val.as_objclass();
            auto * subclass = peek_value(0).as_objclass();

            for (const auto & [name, method] : superclass->all_methods()) {
                subclass->add_method(name, method);
            }

            pop_value(); // subclass

            break;
        }
        case Method: {
            define_method(read_constant().as_string());
            break;
        }
        }

        if (op_result != InterpretResult::Ok) {
            return op_result;
        }
    }
}

auto init_vm() -> void
{
    g_vm.stack.clear();

    define_native("clock", [](std::span<const Value> /* args */) {
        using namespace std::chrono;

        constexpr static auto MS_IN_SECS = 1'000;

        return Value::number(
                static_cast<double>(
                        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()
                )
                / MS_IN_SECS
        );
    });
}

auto free_vm() -> void
{
    for (auto * obj : g_vm.objects) {
        release_object(obj);
    }
    g_vm.objects.clear();
}

namespace {

template <class... Ts> struct overloaded : Ts...
{
    using Ts::operator()...;
};

} // namespace

auto load_code(std::span<const Code> code) -> ObjFunction *
{
    std::unordered_map<SourceLocation, ObjFunction *> functions;
    for (const auto & c : code) {
        functions.emplace(c.get_location(), new_object<ObjFunction>(std::string{c.get_name()}));
    }

    const auto into_runtime_value = [&functions](const CompiledValue & value) -> Value {
        return std::visit(
                overloaded{
                        [](const std::string & s) -> Value {
                            return Value::obj(new_object<ObjString>(s));
                        },
                        [](double n) -> Value { return Value::number(n); },
                        [&functions](const FunctionReference & ref) -> Value {
                            return Value::obj(functions[ref.sloc]);
                        },
                },
                value
        );
    };

    for (const auto & c : code) {
        ObjFunction * func = functions[c.get_location()];
        func->arity() = c.arity();
        func->upvalue_count() = c.upvalue_count();
        func->get_chunk() = {
                c.code() | std::ranges::to<std::vector>(),
                c.locations() | std::ranges::to<std::vector>(),
                c.constants() | std::views::transform(into_runtime_value)
                        | std::ranges::to<std::vector>(),
        };

        if (DEBUG_PRINT_CODE) [[unlikely]] {
            disassemble_chunk(
                    func->get_chunk(), func->get_name().empty() ? "<script>" : func->get_name()
            );
        }
    }

    // TODO: stricter way to define entrypoint function?
    return functions[code.back().get_location()];
}

auto interpret(std::string_view source) -> InterpretResult
{
    auto code = compile(source);
    if (!code.has_value()) {
        return InterpretResult::CompileError;
    }
    auto * function = load_code(code.value());

    g_vm.gc_active = true;

    // FIXME: hack. Should use arrays inside VM object instead.
    g_vm.frames.reserve(FRAMES_MAX);
    g_vm.stack.reserve(STACK_MAX);

    push_value(Value::obj(function));
    auto * closure = new_object<ObjClosure>(function);
    pop_value();
    push_value(Value::obj(closure));
    call(*closure, 0);

    return run();
}

} // namespace cpplox
