module;

#include <cassert>

export module cpplox2:VirtualMachine;

import std;

import :Chunk;
import :Compiler;
import :Debug;
import :Value;

namespace cpplox {

namespace {
constexpr const std::size_t STACK_MAX = 256;
constexpr const bool DEBUG_VM_EXECUTION = true;
} // namespace

export struct VirtualMachine
{
    const Chunk * chunk = nullptr;
    const Byte * ip = nullptr;
    std::vector<Value> stack;
};

export enum class [[nodiscard]] InterpretResult : std::uint8_t {
    Ok,
    CompileError,
    RuntimeError,
};

// FIXME: get rid of singleton instance
VirtualMachine g_vm; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

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

template <OpCode op> auto binary_op() -> void
{
    Value rhs = pop_value();
    Value lhs = pop_value();
    if constexpr (op == OpCode::Add) {
        push_value(lhs + rhs);
    }
    if constexpr (op == OpCode::Substract) {
        push_value(lhs - rhs);
    }
    if constexpr (op == OpCode::Multiply) {
        push_value(lhs * rhs);
    }
    if constexpr (op == OpCode::Divide) {
        push_value(lhs / rhs);
    }
}

} // namespace

auto run() -> InterpretResult
{
    using enum OpCode;

    for (;;) {
        if constexpr (DEBUG_VM_EXECUTION) {
            print_stack(g_vm.stack);
            disassemble_instruction(*g_vm.chunk, get_ip_offset());
        }

        switch (static_cast<OpCode>(advance_ip())) {
        case Constant: {
            Value constant = g_vm.chunk->constants[advance_ip()];
            push_value(constant);
            break;
        }
        // Binary ops
        case Add: binary_op<Add>(); break;
        case Substract: binary_op<Substract>(); break;
        case Multiply: binary_op<Multiply>(); break;
        case Divide: binary_op<Divide>(); break;
        // Unary ops
        case Negate: push_value(-pop_value()); break;
        //
        case Return: {
            std::println("OUT: {}", pop_value());
            return InterpretResult::Ok;
        }
        }
    }
}

export auto interpret(std::string_view source) -> InterpretResult
{
    compile(source);
    return InterpretResult::Ok;
    // g_vm.chunk = &chunk;
    // g_vm.ip = chunk.code.data();
    // return run();
}

} // namespace cpplox
