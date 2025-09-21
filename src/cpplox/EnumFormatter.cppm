export module cpplox:EnumFormatter;

import std;

import magic_enum;

namespace cpplox {

export template <typename T>
concept IsEnum = std::is_enum_v<T>;

export template <IsEnum E> struct EnumFormatter : std::formatter<std::string_view>
{
    auto format(const E & e, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format(magic_enum::enum_name(e), ctx);
    }
};

} // namespace cpplox
