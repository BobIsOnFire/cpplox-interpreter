module cpplox:RuntimeError;

import std;

import :Token;

namespace cpplox {

export class RuntimeError : public std::runtime_error
{
public:
    explicit RuntimeError(Token token, std::string_view what = "")
        : std::runtime_error(what.data())
        , m_token(std::move(token))
    {
    }

    [[nodiscard]] auto get_token() const -> const Token & { return m_token; }

private:
    Token m_token;
};

} // namespace cpplox
