module;

#include <cassert>

export module cpplox:Chunk;

import std;

import :OpCode;
import :SourceLocation;
import :Value;

namespace cpplox {

export struct Chunk
{
    std::vector<Byte> code;
    std::vector<SourceLocation> locations;
    std::vector<Value> constants;
};

export auto write_chunk(Chunk & chunk, Byte data, SourceLocation sloc) -> void;
export auto write_chunk(Chunk & chunk, OpCode op, SourceLocation sloc) -> void;
export auto add_constant(Chunk & chunk, Value value) -> std::size_t;

} // namespace cpplox
