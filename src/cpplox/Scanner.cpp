module cpplox:Scanner;

import :Token;

import std;

namespace cpplox {

export class Scanner
{
public:
    explicit Scanner(std::string source)
        : m_source(std::move(source))
    {
    }

    [[nodiscard]] auto scan_tokens() const -> std::vector<Token>
    {
        return std::string_view{m_source} | std::views::split(std::string_view{" "})
                | std::views::transform([](auto r) { return Token(std::string_view{r}); })
                | std::ranges::to<std::vector>();
    }

private:
    std::string m_source;
};

} // namespace cpplox
