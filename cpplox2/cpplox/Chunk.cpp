module;

#include <cassert>

export module cpplox2:Chunk;

import std;

import :SourceLocation;
import :Value;

namespace cpplox {

export using Byte = std::uint8_t;

export enum class OpCode : Byte {
    Constant,
    Return,
};

export struct Chunk
{
    std::vector<Byte> code;
    std::vector<SourceLocation> locations;
    std::vector<Value> constants;
};

export auto write_chunk(Chunk & chunk, Byte data, SourceLocation sloc) -> void
{
    chunk.code.push_back(data);
    chunk.locations.push_back(sloc);
}

export auto write_chunk(Chunk & chunk, OpCode op, SourceLocation sloc) -> void
{
    write_chunk(chunk, static_cast<Byte>(op), sloc);
}

export auto add_constant(Chunk & chunk, Value value) -> Byte
{
    assert(chunk.constants.size() < sizeof(Byte));
    chunk.constants.push_back(value);
    return static_cast<Byte>(chunk.constants.size() - 1);
}

} // namespace cpplox
