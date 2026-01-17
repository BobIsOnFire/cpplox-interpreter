export module cpplox:Code;

import std;

import :OpCode;
import :SourceLocation;

namespace cpplox {

struct FunctionReference
{
    std::string name;
    SourceLocation sloc;
};

using CompiledValue = std::variant<std::string, double, FunctionReference>;

class Code
{
public:
    explicit Code(std::string name, SourceLocation sloc)
        : m_name(std::move(name))
        , m_sloc(sloc)
    {
    }

    auto write(Byte data, SourceLocation sloc) -> void;
    auto write(OpCode op, SourceLocation sloc) -> void;

    auto add_constant(CompiledValue value) -> std::size_t;

    [[nodiscard]] auto code() const -> std::span<const Byte> { return m_code; }
    [[nodiscard]] auto code() -> std::span<Byte> { return m_code; }

    [[nodiscard]] auto locations() const -> std::span<const SourceLocation> { return m_locations; }
    [[nodiscard]] auto constants() const -> std::span<const CompiledValue> { return m_constants; }

    [[nodiscard]] constexpr auto get_name() const -> std::string_view { return m_name; }
    [[nodiscard]] constexpr auto get_location() const -> SourceLocation { return m_sloc; }

    template <class Self> [[nodiscard]] auto get_chunk(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_chunk;
    }

    template <class Self> [[nodiscard]] auto arity(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_arity;
    }

    template <class Self> [[nodiscard]] auto upvalue_count(this Self && self) -> auto &&
    {
        return std::forward<Self>(self).m_upvalue_count;
    }

private:
    std::string m_name;
    SourceLocation m_sloc;

    std::size_t m_arity = 0;
    std::size_t m_upvalue_count = 0;

    std::vector<Byte> m_code;
    std::vector<SourceLocation> m_locations;
    std::vector<CompiledValue> m_constants;
};

} // namespace cpplox
