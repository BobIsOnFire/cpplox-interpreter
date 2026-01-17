export module cpplox:Chunk;

import std;

import :OpCode;
import :SourceLocation;
import :Value;

namespace cpplox {

export class Chunk
{
public:
    Chunk() = default;

    Chunk(std::vector<Byte> code,
          std::vector<SourceLocation> locations,
          std::vector<Value> constants)
        : m_code(std::move(code))
        , m_locations(std::move(locations))
        , m_constants(std::move(constants))
    {
    }

    [[nodiscard]] auto code() const -> std::span<const Byte> { return m_code; }
    [[nodiscard]] auto locations() const -> std::span<const SourceLocation> { return m_locations; }
    [[nodiscard]] auto constants() const -> std::span<const Value> { return m_constants; }

private:
    std::vector<Byte> m_code;
    std::vector<SourceLocation> m_locations;
    std::vector<Value> m_constants;
};

} // namespace cpplox
