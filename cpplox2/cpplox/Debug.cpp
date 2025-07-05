export module cpplox2:Debug;

import std;

import :Chunk;

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

auto disassemble_instruction(const Chunk & chunk, std::size_t offset) -> std::size_t
{
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
    case OpCode::Constant: return constant("OP_CONSTANT", chunk, offset);
    case OpCode::Return: return simple("OP_RETURN", offset);
    default: std::println("Unknown opcode {:x}", static_cast<Byte>(instruction)); return offset + 1;
    }
}

} // namespace

export auto disassemble_chunk(const Chunk & chunk, std::string_view chunk_name) -> void
{
    std::println("== {} ==", chunk_name);

    for (std::size_t offset = 0; offset < chunk.code.size();) {
        offset = disassemble_instruction(chunk, offset);
    }
}

} // namespace cpplox
