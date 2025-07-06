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
    std::vector<std::pair<SourceLocation, std::size_t>> locations; // run-length encoded
    std::vector<Value> constants;
};

export auto write_chunk(Chunk & chunk, Byte data, SourceLocation sloc) -> void
{
    chunk.code.push_back(data);
    if (chunk.locations.size() > 0 && chunk.locations.back().first.line == sloc.line) {
        chunk.locations.back().second++;
    }
    else {
        chunk.locations.emplace_back(sloc, 1);
    }
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

export auto get_source_location(const Chunk & chunk, std::size_t idx) -> SourceLocation
{
    assert(idx < chunk.code.size());
    std::size_t i = 0;
    for (const auto & [sloc, len] : chunk.locations) {
        i += len;
        if (i > idx) {
            return sloc;
        }
    }
    std::unreachable();
}

} // namespace cpplox
