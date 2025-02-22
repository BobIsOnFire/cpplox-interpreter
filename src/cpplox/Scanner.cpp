module cpplox:Scanner;

import std;

import fast_float;

import :Lox;
import :Token;

namespace cpplox {

namespace {

constexpr auto is_digit(char c) -> bool { return c >= '0' && c <= '9'; }
constexpr auto is_alpha(char c) -> bool
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
constexpr auto is_alnum(char c) -> bool { return is_digit(c) || is_alpha(c); }

} // namespace

export class Scanner
{
public:
    explicit Scanner(std::string source)
        : m_source(std::move(source))
    {
    }

    [[nodiscard]] auto scan_tokens() [[clang::lifetimebound]] -> std::vector<Token>
    {
        while (!is_at_end()) {
            m_start = m_current;
            scan_token();
        }

        m_tokens.emplace_back("", m_line, TokenType::EndOfFile);
        return m_tokens;
    }

private:
    [[nodiscard]] auto is_at_end() const -> bool { return m_current >= m_source.length(); }

    auto advance() -> char { return m_source[m_current++]; }
    [[nodiscard]] auto peek() const -> char
    {
        if (is_at_end()) {
            return '\0';
        }
        return m_source[m_current];
    }
    [[nodiscard]] auto peek_next() const -> char
    {
        if (m_current + 1 >= m_source.length()) {
            return '\0';
        }
        return m_source[m_current + 1];
    }
    auto match(char expected) -> bool
    {
        if (is_at_end()) {
            return false;
        }

        if (m_source[m_current] != expected) {
            return false;
        }

        m_current++;
        return true;
    }

    auto scan_token() -> void
    {
        using enum TokenType;
        char c = advance();
        switch (c) {
        // white-space
        case ' ':
        case '\r':
        case '\t': break;
        case '\n': m_line++; break;

        // context-free single-character tokens
        case '(': add_token(LeftParenthesis); break;
        case ')': add_token(RightParenthesis); break;
        case '{': add_token(LeftBrace); break;
        case '}': add_token(RightBrace); break;
        case ',': add_token(Comma); break;
        case '.': add_token(Dot); break;
        case '-': add_token(Minus); break;
        case '+': add_token(Plus); break;
        case ';': add_token(Semicolon); break;
        case '*': add_token(Star); break;
        // slash is a special-case: two continuous slashes define a comment
        case '/':
            if (match('/')) {
                while (peek() != '\n' && !is_at_end()) {
                    advance();
                }
            }
            else {
                add_token(Slash);
            }
            break;

        // choose a token if next character matches a token continuation
        case '!': add_token(match('=') ? BangEqual : Bang); break;
        case '=': add_token(match('=') ? EqualEqual : Equal); break;
        case '<': add_token(match('=') ? LessEqual : Less); break;
        case '>': add_token(match('=') ? GreaterEqual : Greater); break;

        // more complicated stuff
        case '"': add_string(); break;

        default:
            if (is_digit(c)) {
                add_number();
            }
            else if (is_alpha(c)) {
                add_identifier();
            }
            else {
                error("Unexpected character.");
            }
            break;
        }
    }

    auto add_string() -> void
    {
        while (peek() != '"' && !is_at_end()) {
            if (peek() == '\n') {
                m_line++;
            }
            advance();
        }

        if (is_at_end()) {
            error("Unterminated string.");
            return;
        }

        // Consume closing quote
        advance();

        // Make sure the literal is added without surrounding quotes
        auto literal = m_source.substr(m_start + 1, m_current - m_start - 2);
        add_token(TokenType::String, literal);
    }

    auto add_number() -> void
    {
        while (is_digit(peek())) {
            advance();
        }

        // Fractional part
        if (peek() == '.' && is_digit(peek_next())) {
            advance(); // consume the '.'
            while (is_digit(peek())) {
                advance();
            }
        }

        double num = 0;
        auto answer = fast_float::from_chars(&m_source[m_start], &m_source[m_current], num);

        if (answer.ec != std::errc()) {
            error("Parsing failure");
            return;
        }

        add_token(TokenType::Number, num);
    }

    auto add_identifier() -> void
    {
        using enum TokenType;
        static std::unordered_map<std::string_view, TokenType> keywords = {
                {"and", And},
                {"class", Class},
                {"else", Else},
                {"false", False},
                {"for", For},
                {"fun", Fun},
                {"if", If},
                {"nil", Nil},
                {"or", Or},
                {"print", Print},
                {"return", Return},
                {"super", Super},
                {"this", This},
                {"true", True},
                {"var", Var},
                {"while", While},
        };

        while (is_alnum(peek())) {
            advance();
        }

        if (auto type = keywords.find(get_lexeme()); type != keywords.end()) {
            add_token(type->second);
        }
        else {
            add_token(TokenType::Identifier);
        }
    }

    auto add_token(TokenType type, Token::Literal literal = Token::EmptyLiteral{}) -> void
    {
        m_tokens.emplace_back(get_lexeme(), m_line, type, std::move(literal));
    }

    [[nodiscard]] auto get_lexeme() const -> std::string_view
    {
        return std::string_view{m_source}.substr(m_start, m_current - m_start);
    }

    auto error(std::string_view message) const -> void { Lox::instance()->error(m_line, message); }

    std::string m_source;
    std::vector<Token> m_tokens;

    std::size_t m_start = 0;
    std::size_t m_current = 0;
    std::size_t m_line = 1;
};

} // namespace cpplox
