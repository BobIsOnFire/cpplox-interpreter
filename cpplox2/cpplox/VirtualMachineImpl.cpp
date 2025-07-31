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
constexpr const std::size_t STACK_MAX = 256;
constexpr const bool DEBUG_VM_EXECUTION = true;
} // namespace

namespace {

auto advance_ip() -> Byte
{
    Byte b = *g_vm.ip;
    std::advance(g_vm.ip, 1);
    return b;
}

auto get_ip_offset() -> std::size_t
{
    return static_cast<std::size_t>(std::distance(g_vm.chunk->code.data(), g_vm.ip));
}

template <typename... Args> auto runtime_error(std::format_string<Args...> fmt, Args &&... args)
{
    const auto & location = g_vm.chunk->locations[get_ip_offset()];
    std::print(std::cerr, "[{}:{}] error: ", location.line, location.column);
    std::println(std::cerr, fmt, std::forward(args)...);

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

} // namespace

auto run() -> InterpretResult
{
    using enum OpCode;

    for (;;) {
        InterpretResult op_result = InterpretResult::Ok;

        if constexpr (DEBUG_VM_EXECUTION) {
            print_stack(g_vm.stack);
            disassemble_instruction(*g_vm.chunk, get_ip_offset());
        }

        switch (static_cast<OpCode>(advance_ip())) {
        // Values
        case Constant: {
            Value constant = g_vm.chunk->constants[advance_ip()];
            push_value(constant);
            break;
        }
        case Nil: push_value(Value::nil()); break;
        case True: push_value(Value::boolean(true)); break;
        case False: push_value(Value::boolean(false)); break;
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
                std::string rhs = pop_value().as_string();
                std::string lhs = pop_value().as_string();
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
        case Return: {
            std::println("OUT: {}", pop_value());
            return InterpretResult::Ok;
        }
        }

        if (op_result != InterpretResult::Ok) {
            return op_result;
        }
    }
}

export auto init_vm() -> void { g_vm.stack.clear(); }

export auto free_vm() -> void
{
    for (const auto * obj : g_vm.objects) {
        delete obj;
    }
    g_vm.objects.clear();
}

export auto interpret(std::string_view source) -> InterpretResult
{
    auto chunk = compile(source);
    if (!chunk.has_value()) {
        return InterpretResult::CompileError;
    }

    g_vm.chunk = &*chunk;
    g_vm.ip = chunk->code.data();
    auto result = run();

    return result;
}

} // namespace cpplox
