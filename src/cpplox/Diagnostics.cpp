module cpplox:Diagnostics;

import std;

import :RuntimeError;
import :Token;

namespace cpplox {

export class Diagnostics
{
public:
    Diagnostics(Diagnostics const &) = delete;
    Diagnostics(Diagnostics &&) = delete;
    auto operator=(Diagnostics const &) -> Diagnostics & = delete;
    auto operator=(Diagnostics &&) -> Diagnostics & = delete;

private:
    Diagnostics() = default;
    ~Diagnostics() = default;

public:
    static auto instance() -> Diagnostics *
    {
        static Diagnostics s_instance;
        return &s_instance;
    }

    [[nodiscard]] auto has_errors() const -> bool { return m_has_errors; }
    [[nodiscard]] auto has_runtime_errors() const -> bool { return m_has_runtime_errors; }

    auto error(const Token & token, std::string_view message) -> void
    {
        if (token.get_type() == TokenType::EndOfFile) {
            report(token.get_line(), " at end", message);
        }
        else {
            report(token.get_line(), std::format(" at '{}'", token.get_lexeme()), message);
        }
    }

    auto error(std::size_t line, std::string_view message) -> void { report(line, "", message); }

    auto runtime_error(const RuntimeError & error) -> void
    {
        std::println(std::cerr, "{}\n[line {}]", error.what(), error.get_token().get_line());
        m_has_runtime_errors = true;
    }

private:
    auto report(std::size_t line, std::string_view where, std::string_view message) -> void
    {
        std::println(std::cerr, "[line {}] Error{}: {}", line, where, message);
        m_has_errors = true;
    }

    bool m_has_errors = false;
    bool m_has_runtime_errors = false;
};

} // namespace cpplox
