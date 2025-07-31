export module cpplox2:Debug;

import std;

import :Chunk;
import :ValueFormatter;

namespace cpplox {

namespace {

auto simple(std::string_view name, std::size_t offset) -> std::size_t
{
    std::println("{}", name);
    return offset + 1;
}

auto constant(std::string_view name, const Chunk & chunk, std::size_t offset) -> std::size_t
{
    Byte constant_idx = chunk.code[offset + 1];
    std::println("{:16} {:4} '{}'", name, constant_idx, chunk.constants[constant_idx]);
    return offset + 2;
}

auto byte(std::string_view name, const Chunk & chunk, std::size_t offset) -> std::size_t
{
    Byte slot = chunk.code[offset + 1];
    std::println("{:16} {:4}", name, slot);
    return offset + 2;
}

} // namespace

export auto print_stack(std::span<const Value> stack_view) -> void
{
    std::print("{:15}", ' ');
    if (stack_view.empty()) {
        std::println("<stack empty>");
        return;
    }
    for (const auto & value : stack_view) {
        std::print("[ {} ]", value);
    }
    std::println();
}

export auto disassemble_instruction(const Chunk & chunk, std::size_t offset) -> std::size_t
{
    using enum OpCode;

    std::print("{:04} ", offset);
    auto sloc = chunk.locations[offset];
    if (offset > 0 && sloc.line == chunk.locations[offset - 1].line) {
        std::print("{:>4}:{:<4} ", '|', sloc.column);
    }
    else {
        std::print("{:>4}:{:<4} ", sloc.line, sloc.column);
    }

    auto instruction = static_cast<OpCode>(chunk.code[offset]);

    switch (instruction) {
    // Values
    case Constant: return constant("OP_CONSTANT", chunk, offset);
    case Nil: return simple("OP_NIL", offset);
    case True: return simple("OP_TRUE", offset);
    case False: return simple("OP_FALSE", offset);
    // Value manipulators
    case Pop: return simple("OP_POP", offset);
    case DefineGlobal: return constant("OP_DEFINE_GLOBAL", chunk, offset);
    case GetGlobal: return constant("OP_GET_GLOBAL", chunk, offset);
    case GetLocal: return byte("OP_GET_LOCAL", chunk, offset);
    case SetGlobal: return constant("OP_SET_GLOBAL", chunk, offset);
    case SetLocal: return byte("OP_SET_LOCAL", chunk, offset);
    // Comparison ops
    case Equal: return simple("OP_EQUAL", offset);
    case Less: return simple("OP_LESS", offset);
    case Greater: return simple("OP_GREATER", offset);
    // Binary ops
    case Add: return simple("OP_ADD", offset);
    case Substract: return simple("OP_SUBSTRACT", offset);
    case Multiply: return simple("OP_MULTIPLY", offset);
    case Divide: return simple("OP_DIVIDE", offset);
    // Unary ops
    case Not: return simple("OP_NOT", offset);
    case Negate: return simple("OP_NEGATE", offset);
    // Aux
    case Print: return simple("OP_PRINT", offset);
    case Return: return simple("OP_RETURN", offset);
    }

    std::println("Unknown opcode {:x}", static_cast<Byte>(instruction));
    return offset + 1;
}

export auto disassemble_chunk(const Chunk & chunk, std::string_view chunk_name) -> void
{
    std::println("== {} ==", chunk_name);

    for (std::size_t offset = 0; offset < chunk.code.size();) {
        offset = disassemble_instruction(chunk, offset);
    }
}

} // namespace cpplox
