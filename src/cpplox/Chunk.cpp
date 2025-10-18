module cpplox;

import :Chunk;
import :OpCode;
import :Value;

namespace cpplox {

auto Chunk::write(Byte data, SourceLocation sloc) -> void
{
    m_code.push_back(data);
    m_locations.push_back(sloc);
}

auto Chunk::write(OpCode op, SourceLocation sloc) -> void { write(static_cast<Byte>(op), sloc); }

auto Chunk::add_constant(Value value) -> std::size_t
{
    m_constants.push_back(value);
    return m_constants.size() - 1;
}

} // namespace cpplox
