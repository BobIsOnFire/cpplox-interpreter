module;

#include <cassert>

export module cpplox2:VirtualMachineImpl;

import std;

import :Compiler;
import :Debug;
import :Object;
import :VirtualMachine;

namespace cpplox {

namespace {
constexpr const std::size_t FRAMES_MAX = 64;
constexpr const std::size_t STACK_MAX = 256;
constexpr const bool DEBUG_VM_EXECUTION = false;
} // namespace

namespace {

auto current_frame() -> CallFrame & { return g_vm.frames.back(); }
auto current_chunk() -> Chunk & { return current_frame().closure->get_function()->get_chunk(); }

auto read_byte() -> Byte
{
    Byte b = *current_frame().ip;
    std::advance(current_frame().ip, 1);
    return b;
}

auto read_instruction() -> OpCode { return static_cast<OpCode>(read_byte()); }

auto read_constant() -> Value { return current_chunk().constants[read_byte()]; }

auto read_double_byte() -> DoubleByte
{
    return static_cast<DoubleByte>(read_byte() << BYTE_DIGITS) | read_byte();
}

template <typename... Args> auto runtime_error(std::format_string<Args...> fmt, Args &&... args)
{
    std::println(std::cerr, fmt, std::forward<Args>(args)...);

    for (const auto & frame : std::ranges::reverse_view{g_vm.frames}) {
        const auto * function = frame.closure->get_function();

        const auto & chunk = function->get_chunk();
        auto chunk_offset = static_cast<std::size_t>(std::distance(chunk.code.data(), frame.ip));

        const auto & location = chunk.locations[chunk_offset];
        std::print(std::cerr, "[{}:{}] in ", location.line, location.column);

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
            .ip = function.get_chunk().code.data(),
            .slots = &g_vm.stack[slot_start],
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
        g_vm.stack[g_vm.stack.size() - arg_count - 1] = Value::instance(cls);

        auto init = cls->get_method("init");
        if (init.has_value()) {
            return call(*init->as_objclosure(), arg_count);
        }

        if (arg_count != 0) {
            runtime_error("Expected 0 arguments but got {}.", arg_count);
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

    auto * bound = ObjBoundMethod::create(peek_value(), method.value().as_objclosure());

    pop_value();
    push_value(Value::obj(bound));

    return true;
}

auto capture_upvalue(Value * local) -> ObjUpvalue *
{
    ObjUpvalue * prev = nullptr;
    ObjUpvalue * upvalue = g_vm.open_upvalues;

    // well this is a huge pile of ptr comparison bullshit idk
    while (upvalue != nullptr && upvalue->location() > local) {
        prev = upvalue;
        upvalue = upvalue->next();
    }

    if (upvalue != nullptr && upvalue->location() == local) {
        return upvalue;
    }

    auto * created_upvalue = ObjUpvalue::create(local);
    created_upvalue->set_next(upvalue);

    if (prev == nullptr) {
        g_vm.open_upvalues = created_upvalue;
    }
    else {
        prev->set_next(created_upvalue);
    }

    return created_upvalue;
}

auto close_upvalues(Value * last) -> void
{
    while (g_vm.open_upvalues != nullptr && g_vm.open_upvalues->location() >= last) {
        auto * upvalue = g_vm.open_upvalues;
        upvalue->close();
        g_vm.open_upvalues = upvalue->next();
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
    push_value(Value::string(std::string{name}));
    push_value(Value::native(callable));
    g_vm.globals.emplace(name, g_vm.stack.back());
    pop_value();
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

        if constexpr (DEBUG_VM_EXECUTION) {
            print_stack(g_vm.stack);

            const auto * chunk_start = current_chunk().code.data();
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
            g_vm.globals.emplace(name, peek_value());
            pop_value();
            break;
        }
        case GetGlobal: {
            const std::string & name = read_constant().as_string();
            auto it = g_vm.globals.find(name);
            if (it == g_vm.globals.end()) {
                runtime_error("Undefined variable '{}'", name);
                return InterpretResult::RuntimeError;
            }
            push_value(it->second);
            break;
        }
        case GetLocal: {
            Byte slot = read_byte();
            push_value(current_frame().slots[slot]);
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
        case GetUpvalue: {
            Byte slot = read_byte();
            push_value(*current_frame().closure->upvalues()[slot]->location());
            break;
        }
        case SetGlobal: {
            const std::string & name = read_constant().as_string();
            auto it = g_vm.globals.find(name);
            if (it == g_vm.globals.end()) {
                runtime_error("Undefined variable '{}'", name);
                return InterpretResult::RuntimeError;
            }
            it->second = peek_value();
            break;
        }
        case SetLocal: {
            Byte slot = read_byte();
            current_frame().slots[slot] = peek_value();
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
                auto value = Value::string(lhs + rhs);
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
                runtime_error("Operator must be a number.");
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
        case Closure: {
            auto * function = read_constant().as_objfunction();
            auto * closure = ObjClosure::create(function);
            push_value(Value::obj(closure));

            for (auto _ : std::views::iota(0UZ, function->upvalue_count())) {
                bool is_local = read_byte() == 1;
                Byte index = read_byte();
                if (is_local) {
                    closure->add_upvalue(capture_upvalue(&current_frame().slots[index]));
                }
                else {
                    closure->add_upvalue(current_frame().closure->upvalues()[index]);
                }
            }
            break;
        }
        case CloseUpvalue: {
            close_upvalues(&g_vm.stack.back());
            pop_value();
            break;
        }
        case Return: {
            Value result = pop_value();
            auto * old_slots = g_vm.frames.back().slots;
            close_upvalues(old_slots);

            g_vm.frames.pop_back();
            if (g_vm.frames.empty()) {
                pop_value();
                return InterpretResult::Ok;
            }

            // FIXME: yeah, dirt. Should be solved if we actually use array for stack
            while (&*g_vm.stack.end() != old_slots) {
                pop_value();
            }
            push_value(result);
            break;
        }
        case Class: {
            auto * name = read_constant().as_objstring();
            push_value(Value::cls(name));
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

export auto init_vm() -> void
{
    g_vm.stack.clear();

    define_native("clock", [](std::span<const Value> /* args */) {
        using namespace std::chrono;
        return Value::number(
                static_cast<double>(
                        duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
                )
        );
    });
}

export auto free_vm() -> void
{
    for (auto * obj : g_vm.objects) {
        release_object(obj);
    }
    g_vm.objects.clear();
}

export auto interpret(std::string_view source) -> InterpretResult
{
    auto * function = compile(source);
    if (function == nullptr) {
        return InterpretResult::CompileError;
    }

    // FIXME: hack. Should use arrays inside VM object instead.
    g_vm.frames.reserve(FRAMES_MAX);
    g_vm.stack.reserve(STACK_MAX);

    push_value(Value::obj(function));
    auto * closure = ObjClosure::create(function);
    pop_value();
    push_value(Value::obj(closure));
    call(*closure, 0);

    return run();
}

} // namespace cpplox
