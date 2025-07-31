module;

#include <cassert>

export module cpplox2:Chunk;

import std;

import :SourceLocation;
import :Value;

namespace cpplox {

export using Byte = std::uint8_t;

export enum class OpCode : Byte {
    // Values
    Constant,
    Nil,
    True,
    False,
    // Value manipulators
    Pop,
    DefineGlobal,
    GetGlobal,
    SetGlobal,
    // Comparison ops
    Equal,
    Greater,
    Less,
    // Binary ops
    Add,
    Substract,
    Multiply,
    Divide,
    // Unary ops
    Not,
    Negate,
    // Aux
    Print,
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

export auto add_constant(Chunk & chunk, Value value) -> std::size_t
{
    chunk.constants.push_back(value);
    return chunk.constants.size() - 1;
}

} // namespace cpplox
