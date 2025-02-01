export module cpplox.Token;

import std;

namespace cpplox {

export class Token
{
public:
    explicit Token(std::string_view data)
        : m_data(data)
    {
    }

    [[nodiscard]] auto data() const -> std::string_view { return m_data; }

private:
    std::string_view m_data;
};

} // namespace cpplox

template <> struct std::formatter<cpplox::Token> : std::formatter<std::string_view>
{
    auto format(const cpplox::Token & token, std::format_context & ctx) const
    {
        return std::formatter<std::string_view>::format(token.data(), ctx);
    }
};
