export module cpplox:Debug;

import std;

import :Chunk;
import :Value;

namespace cpplox {

export auto print_stack(std::span<const Value> stack_view) -> void;
export auto disassemble_instruction(const Chunk & chunk, std::size_t offset) -> std::size_t;
export auto disassemble_chunk(const Chunk & chunk, std::string_view chunk_name) -> void;

} // namespace cpplox
