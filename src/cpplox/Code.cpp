module cpplox;

import std;

import :Code;

namespace cpplox {

auto Code::write(Byte data, SourceLocation sloc) -> void
{
    m_code.push_back(data);
    m_locations.push_back(sloc);
}

auto Code::write(OpCode op, SourceLocation sloc) -> void { write(static_cast<Byte>(op), sloc); }

auto Code::add_constant(CompiledValue value) -> std::size_t
{
    m_constants.push_back(std::move(value));
    return m_constants.size() - 1;
}

}
