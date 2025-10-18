export module cpplox:Chunk;

import std;

import :OpCode;
import :SourceLocation;
import :Value;

namespace cpplox {

export class Chunk
{
public:
    auto write(Byte data, SourceLocation sloc) -> void;
    auto write(OpCode op, SourceLocation sloc) -> void;

    auto add_constant(Value value) -> std::size_t;

    [[nodiscard]] auto code() const -> std::span<const Byte> { return m_code; }
    [[nodiscard]] auto code() -> std::span<Byte> { return m_code; }

    [[nodiscard]] auto locations() const -> std::span<const SourceLocation> { return m_locations; }
    [[nodiscard]] auto constants() const -> std::span<const Value> { return m_constants; }

private:
    std::vector<Byte> m_code;
    std::vector<SourceLocation> m_locations;
    std::vector<Value> m_constants;
};

} // namespace cpplox
