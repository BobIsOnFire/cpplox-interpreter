module cpplox;

import :Chunk;
import :Value;

namespace cpplox {

auto write_chunk(Chunk & chunk, Byte data, SourceLocation sloc) -> void
{
    chunk.code.push_back(data);
    chunk.locations.push_back(sloc);
}

auto write_chunk(Chunk & chunk, OpCode op, SourceLocation sloc) -> void
{
    write_chunk(chunk, static_cast<Byte>(op), sloc);
}

auto add_constant(Chunk & chunk, Value value) -> std::size_t
{
    chunk.constants.push_back(value);
    return chunk.constants.size() - 1;
}

} // namespace cpplox
