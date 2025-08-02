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
auto current_chunk() -> Chunk & { return current_frame().function->get_chunk(); }

auto read_byte() -> Byte
{
    Byte b = *current_frame().ip;
    std::advance(current_frame().ip, 1);
    return b;
}

auto read_instruction() -> OpCode { return static_cast<OpCode>(read_byte()); }

auto read_double_byte() -> DoubleByte
{
    return static_cast<DoubleByte>(read_byte() << BYTE_DIGITS) | read_byte();
}

template <typename... Args> auto runtime_error(std::format_string<Args...> fmt, Args &&... args)
{
    std::println(std::cerr, fmt, std::forward<Args>(args)...);

    for (const auto & frame : std::ranges::reverse_view{g_vm.frames}) {
        const auto * function = frame.function;

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

auto get_constant() -> Value { return current_chunk().constants[read_byte()]; }

// FIXME: should return InterpretResult or some other error type?
auto call(ObjFunction & function, Byte arg_count) -> bool
{
    if (arg_count != function.get_arity()) {
        runtime_error("Expected {} arguments but got {}.", function.get_arity(), arg_count);
        return false;
    }

    if (g_vm.frames.size() >= FRAMES_MAX) {
        runtime_error("Stack overflow.");
        return false;
    }

    auto slot_start = g_vm.stack.size() - arg_count - 1;

    g_vm.frames.push_back({
            .function = &function,
            .ip = function.get_chunk().code.data(),
            .slots = &g_vm.stack[slot_start],
    });

    return true;
}

auto call_value(Value callee, Byte arg_count) -> bool
{
    if (callee.is_function()) {
        return call(*callee.as_objfunction(), arg_count);
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
            push_value(get_constant());
            break;
        }
        case Nil: push_value(Value::nil()); break;
        case True: push_value(Value::boolean(true)); break;
        case False: push_value(Value::boolean(false)); break;
        // Value manipulators
        case Pop: pop_value(); break;
        case DefineGlobal: {
            const std::string & name = get_constant().as_string();
            // FIXME: is there a way to get rid of copy on key insert? Key will surely live as long
            // as VM lives
            g_vm.globals.emplace(name, peek_value());
            pop_value();
            break;
        }
        case GetGlobal: {
            const std::string & name = get_constant().as_string();
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
        case SetGlobal: {
            const std::string & name = get_constant().as_string();
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
                const auto & rhs = pop_value().as_string();
                const auto & lhs = pop_value().as_string();
                push_value(Value::string(lhs + rhs));
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
        case Return: {
            Value result = pop_value();
            auto * old_slots = g_vm.frames.back().slots;
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
    for (const auto * obj : g_vm.objects) {
        delete obj;
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
    call(*function, 0);

    return run();
}

} // namespace cpplox
