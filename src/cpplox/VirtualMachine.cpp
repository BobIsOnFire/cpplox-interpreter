module;

#include <cassert>

module cpplox;

import std;

// TODO: replace with std::inplace_vector (C++26) once it's supported
import beman.inplace_vector;

import :Compiler;
import :Debug;
import :Object;
import :ObjReferenceTracer;
import :OpCode;
import :VirtualMachine;

namespace cpplox {

namespace {

constexpr const std::size_t FRAMES_MAX = 64;
constexpr const std::size_t STACK_MAX = 256;

const bool DEBUG_PRINT_CODE = std::getenv("LOX_DEBUG_PRINT_CODE") != nullptr;
const bool DEBUG_VM_EXECUTION = std::getenv("LOX_DEBUG_VM_EXECUTION") != nullptr;
const bool DEBUG_RUN_GC_EVERY_TIME = std::getenv("LOX_DEBUG_RUN_GC_EVERY_TIME") != nullptr;
const bool DEBUG_LOG_GC = std::getenv("LOX_DEBUG_LOG_GC") != nullptr;

constexpr const std::size_t GC_HEAP_INITIAL_THRESHOLD = 1024UZ * 1024;
constexpr const std::size_t GC_HEAP_GROW_FACTOR = 2;

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

template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

} // namespace

class VirtualMachine : public IVirtualMachine
{
private:
    struct Global
    {
        Value value;
        bool is_const;
    };

public:
    VirtualMachine()
    {
        define_native("clock", [](std::span<const Value> /* args */) -> Value {
            using namespace std::chrono;

            constexpr static auto MS_IN_SECS = 1'000;

            return Value::number(
                    static_cast<double>(
                            duration_cast<milliseconds>(system_clock::now().time_since_epoch())
                                    .count()
                    )
                    / MS_IN_SECS
            );
        });
    }

    ~VirtualMachine() override
    {
        for (auto * obj : m_objects) {
            release_object(obj);
        }
    }

    // Disallow copy/move, at least for now. Stuff might get complicated with all that manual memory
    // management.
    // VM object is also stupidly large right now because it stores entire stack and frames
    // in-place.
    VirtualMachine(const VirtualMachine &) = delete;
    VirtualMachine(VirtualMachine &&) = delete;
    auto operator=(const VirtualMachine &) const -> VirtualMachine & = delete;
    auto operator=(VirtualMachine &&) const -> VirtualMachine & = delete;

    auto interpret(std::string_view source) -> InterpretResult override
    {
        auto code = cpplox::compile(source);
        if (!code.has_value()) {
            return InterpretResult::CompileError;
        }

        m_gc_active = false;
        auto * function = load_code(code.value());
        m_gc_active = true;

        push_value(Value::obj(function));
        auto * closure = new_object<ObjClosure>(function);
        pop_value();
        push_value(Value::obj(closure));
        call(*closure, 0);

        return run();
    }

private:
    struct CallFrame
    {
        ObjClosure * closure;
        const Byte * ip;
        std::size_t stack_offset;
    };

private:
    /*** Heap memory management ***/

    auto save_object(Obj * obj) -> void
    {
        if (m_gc_active) {
            if (DEBUG_RUN_GC_EVERY_TIME || m_bytes_allocated >= m_next_gc) {
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

        m_objects.push_back(obj);
        m_bytes_allocated += object_size(obj->get_type());
    }

    auto release_object(Obj * obj) -> void
    {
        auto type = obj->get_type();

        delete obj; // NOLINT(cppcoreguidelines-owning-memory)

        m_bytes_allocated -= object_size(type);

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

    auto collect_garbage() -> void
    {
        if (DEBUG_LOG_GC) [[unlikely]] {
            std::println(std::cerr, "-- gc begin");
        }

        std::size_t before = m_bytes_allocated;

        // Mark

        ObjReferenceTracer tracer({.debug_log_gc = DEBUG_LOG_GC});
        for (const auto & value : m_stack) {
            tracer.trace(value);
        }
        for (const auto & frame : m_frames) {
            tracer.trace(frame.closure);
        }
        for (auto * upvalue : m_open_upvalues) {
            tracer.trace(upvalue);
        }
        for (const auto & [_, global] : m_globals) {
            tracer.trace(global.value);
        }

        // Sweep

        std::vector<Obj *> new_objects;
        for (auto * obj : m_objects) {
            if (tracer.is_referenced(obj)) {
                new_objects.push_back(obj);
            }
            else {
                release_object(obj);
            }
        }

        m_objects = std::move(new_objects);
        m_next_gc = m_bytes_allocated * GC_HEAP_GROW_FACTOR;

        if (DEBUG_LOG_GC) [[unlikely]] {
            std::println(std::cerr, "-- gc end");
            std::println(
                    std::cerr,
                    "   collected {} bytes (from {} to {}), next gc at {}",
                    before - m_bytes_allocated,
                    before,
                    m_bytes_allocated,
                    m_next_gc
            );
        }
    }

    /*** Execution helpers ***/

    auto current_frame() -> CallFrame & { return m_frames.back(); }
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

    template <typename... Args>
    auto runtime_error(std::format_string<Args...> fmt, Args &&... args)
    {
        std::print(std::cerr, "runtime error: ");
        std::println(std::cerr, fmt, std::forward<Args>(args)...);

        for (const auto & frame : std::ranges::reverse_view{m_frames}) {
            const auto * function = frame.closure->get_function();

            const auto & chunk = function->get_chunk();
            auto chunk_offset
                    = static_cast<std::size_t>(std::distance(chunk.code().data(), frame.ip));

            const auto & location = chunk.locations()[chunk_offset - 1];
            std::print(std::cerr, "  [{}:{}] in ", location.line, location.column);

            if (function->get_name().empty()) {
                std::println(std::cerr, "script");
            }
            else {
                std::println(std::cerr, "{}()", function->get_name());
            }
        }

        m_stack.clear();
    }

    auto push_value(Value value) -> void
    {
        assert(m_stack.size() < STACK_MAX && "Value stack overflow");
        m_stack.push_back(value);
    }

    auto pop_value() -> Value
    {
        assert(m_stack.size() > 0 && "Value stack empty");
        Value val = m_stack.back();
        m_stack.pop_back();
        return val;
    }

    auto peek_value(std::size_t distance = 0) -> Value
    {
        assert(m_stack.size() > distance && "Cannot peek, stack is not big enough");
        return m_stack[m_stack.size() - 1 - distance];
    }

    template <OpCode op>
    auto binary_op() -> InterpretResult
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
        if constexpr (op == OpCode::Subtract) {
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

    /*** Function invocation ***/

    // FIXME: should return InterpretResult or some other error type?
    auto call(ObjClosure & closure, Byte arg_count) -> bool
    {
        auto & function = *closure.get_function();
        if (arg_count != function.arity()) {
            runtime_error("Expected {} arguments but got {}.", function.arity(), arg_count);
            return false;
        }

        if (m_frames.size() >= FRAMES_MAX) {
            runtime_error("Stack overflow.");
            return false;
        }

        auto slot_start = m_stack.size() - arg_count - 1;

        m_frames.push_back({
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
            m_stack[m_stack.size() - arg_count - 1] = bound->get_receiver();
            return call(*bound->get_method(), arg_count);
        }
        if (callee.is_class()) {
            auto * cls = callee.as_objclass();
            // place newly created instance before args on the stack to get 'this' resolved
            // correctly to it
            m_stack[m_stack.size() - arg_count - 1] = Value::obj(new_object<ObjInstance>(cls));

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
            std::size_t args_start = m_stack.size() - arg_count;
            Value result = callable(std::span{m_stack}.subspan(args_start, arg_count));

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
            m_stack[m_stack.size() - arg_count - 1] = field.value();
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
        auto it = m_open_upvalues.begin();
        while (it != m_open_upvalues.end() && (*it)->location() > local) {
            it++;
        }

        if (it != m_open_upvalues.end() && (*it)->location() == local) {
            return *it;
        }

        auto * created_upvalue = new_object<ObjUpvalue>(local);
        m_open_upvalues.insert(it, created_upvalue);

        return created_upvalue;
    }

    auto close_upvalues(std::size_t last_offset) -> void
    {
        Value * last_location = &m_stack[last_offset];

        while (!m_open_upvalues.empty() && m_open_upvalues.front()->location() >= last_location) {
            auto * upvalue = m_open_upvalues.front();
            upvalue->close();
            m_open_upvalues.pop_front();
        }
    }

    auto define_method(const std::string & name) -> void
    {
        Value method = peek_value();
        auto * cls = peek_value(1).as_objclass();

        cls->add_method(name, method);
        pop_value(); // once! leave class in place for consequent methods
    }

    auto define_global(const std::string & name, bool is_const) -> InterpretResult
    {
        // FIXME: is there a way to get rid of copy on key insert? Key will surely live as
        // long as VM lives
        auto it = m_globals.find(name);
        if (it != m_globals.end()) {
            if (it->second.is_const) {
                // Allowing to redefine a const variable would circumvent constness checks as it
                // basically allows to provide const variable with a new value
                runtime_error("Cannot redefine global const variable '{}'.", name);
                return InterpretResult::RuntimeError;
            }
            if (is_const) {
                // If mutable is redefined as const, all code that is later called to write into it
                // would result in runtime failure
                runtime_error(
                        "Cannot define global const variable '{}', it is already defined as "
                        "mutable.",
                        name
                );
                return InterpretResult::RuntimeError;
            }
            // Mutable redefined with another mutable, just treat it as regular assignment
            it->second.value = peek_value();
        }
        else {
            m_globals.emplace(name, Global{.value = peek_value(), .is_const = is_const});
        }
        pop_value();
        return InterpretResult::Ok;
    }

    /*** Code loading helpers ***/

    auto define_native(std::string_view name, Value::NativeFn callable) -> void
    {
        // pushing and popping some GC bullsheesh
        push_value(Value::obj(new_object<ObjNative>(callable)));
        m_globals.emplace(name, m_stack.back());
        pop_value();
    }

    auto load_code(std::span<const Code> code) -> ObjFunction *
    {
        std::unordered_map<SourceLocation, ObjFunction *> functions;
        for (const auto & c : code) {
            functions.emplace(c.get_location(), new_object<ObjFunction>(std::string{c.get_name()}));
        }

        const auto into_runtime_value = [this, &functions](const CompiledValue & value) -> Value {
            return std::visit(
                    overloaded{
                            [this](const std::string & s) -> Value {
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

    /*** Main loop ***/

    // Yeah, sucks
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    auto run() -> InterpretResult
    {
        using enum OpCode;

        for (;;) {
            InterpretResult op_result = InterpretResult::Ok;

            if (DEBUG_VM_EXECUTION) [[unlikely]] {
                print_stack(m_stack);

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
            case DefineGlobalConst: {
                auto result = define_global(read_constant().as_string(), /* is_const = */ true);
                if (result != InterpretResult::Ok) {
                    return result;
                }
                break;
            }
            case DefineGlobalVar: {
                auto result = define_global(read_constant().as_string(), /* is_const = */ false);
                if (result != InterpretResult::Ok) {
                    return result;
                }
                break;
            }
            case GetGlobal: {
                const std::string & name = read_constant().as_string();
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    runtime_error("Undefined variable '{}'.", name);
                    return InterpretResult::RuntimeError;
                }
                push_value(it->second.value);
                break;
            }
            case GetLocal: {
                Byte slot = read_byte();
                push_value(m_stack[current_frame().stack_offset + slot]);
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
                auto it = m_globals.find(name);
                if (it == m_globals.end()) {
                    runtime_error("Undefined variable '{}'.", name);
                    return InterpretResult::RuntimeError;
                }
                if (it->second.is_const) {
                    runtime_error("Cannot assign to const variable '{}'.", name);
                    return InterpretResult::RuntimeError;
                }
                it->second.value = peek_value();
                break;
            }
            case SetLocal: {
                Byte slot = read_byte();
                m_stack[current_frame().stack_offset + slot] = peek_value();
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
            case Subtract: op_result = binary_op<Subtract>(); break;
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
            case JumpIfFalseAndPop: {
                DoubleByte offset = read_double_byte();
                if (is_falsey(peek_value())) {
                    std::advance(current_frame().ip, offset);
                }
                pop_value();
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
                                capture_upvalue(&m_stack[current_frame().stack_offset + index])
                        );
                    }
                    else {
                        closure->add_upvalue(current_frame().closure->upvalues()[index]);
                    }
                }
                break;
            }
            case CloseUpvalue: {
                close_upvalues(m_stack.size() - 1);
                pop_value();
                break;
            }
            case Return: {
                Value result = pop_value();
                auto old_offset = m_frames.back().stack_offset;
                close_upvalues(old_offset);

                m_frames.pop_back();
                if (m_frames.empty()) {
                    pop_value();
                    return InterpretResult::Ok;
                }

                // FIXME: yeah, dirt. Should be solved if we actually use array for stack
                while (m_stack.size() != old_offset) {
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

private:
    beman::inplace_vector::inplace_vector<CallFrame, FRAMES_MAX> m_frames;
    beman::inplace_vector::inplace_vector<Value, STACK_MAX> m_stack;
    std::vector<Obj *> m_objects;
    std::unordered_map<std::string, Global> m_globals;
    std::list<ObjUpvalue *> m_open_upvalues;

    std::size_t m_bytes_allocated = 0;
    std::size_t m_next_gc = GC_HEAP_INITIAL_THRESHOLD;
    bool m_gc_active = false;
};

auto make_vm() -> VirtualMachinePtr { return std::make_unique<VirtualMachine>(); }

} // namespace cpplox
