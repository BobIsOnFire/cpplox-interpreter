module cpplox;

import std;

import :Scanner;
import :SourceLocation;

namespace cpplox {

namespace {
auto is_digit(char c) -> bool { return c >= '0' && c <= '9'; }
auto is_alpha(char c) -> bool { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
} // namespace

class Scanner : public IScanner
{
public:
    explicit Scanner(std::string_view source)
        : m_source(source)
    {
    }

    auto next_token() -> Token override
    {
        using enum TokenType;

        skip_whitespace();

        m_start = m_current;
        m_start_sloc = m_sloc;
        if (is_at_end()) {
            return make_token(EndOfFile);
        }

        char c = advance();
        if (is_digit(c)) {
            return number();
        }
        if (is_alpha(c) || c == '_') {
            return identifier();
        }

        switch (c) { // NOLINT(bugprone-switch-missing-default-case,hicpp-multiway-paths-covered)
        case '(': return make_token(LeftParenthesis);
        case ')': return make_token(RightParenthesis);
        case '{': return make_token(LeftBrace);
        case '}': return make_token(RightBrace);
        case ';': return make_token(Semicolon);
        case ',': return make_token(Comma);
        case '.': return make_token(Dot);
        case '-': return make_token(Minus);
        case '+': return make_token(Plus);
        case '/': return make_token(Slash);
        case '*': return make_token(Star);
        case '!': return make_token(match('=') ? BangEqual : Bang);
        case '=': return make_token(match('=') ? EqualEqual : Equal);
        case '<': return make_token(match('=') ? LessEqual : Less);
        case '>': return make_token(match('=') ? GreaterEqual : Greater);
        case '"': return string();
        }

        return error_token("Unexpected character.");
    }

private:
    auto is_at_end() -> bool { return m_current >= m_source.length(); }

    auto advance() -> char
    {
        m_current++;
        m_sloc.column++;
        return m_source[m_current - 1];
    }

    auto peek() -> char { return m_source[m_current]; }

    auto peek_next() -> char
    {
        if (is_at_end()) {
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
        advance();
        return true;
    }

    auto skip_whitespace() -> void
    {
        while (!is_at_end()) {
            char c = peek();
            switch (c) {
            case '\n':
                // TODO: move line tracking into advance() as well
                m_sloc.line++;
                m_sloc.column = 0;
                [[fallthrough]];
            case ' ':
            case '\r':
            case '\t': advance(); break;
            case '/':
                if (peek_next() == '/') {
                    while (!is_at_end() && peek() != '\n') {
                        advance();
                    }
                }
                else {
                    return;
                }
                break;
            default: return;
            }
        }
    }

    auto get_lexeme() -> std::string_view { return m_source.substr(m_start, m_current - m_start); }

    auto make_token(TokenType type) -> Token
    {
        return Token{
                .type = type,
                .lexeme = get_lexeme(),
                .sloc = m_start_sloc,
        };
    }

    auto error_token(std::string_view message) -> Token
    {
        return Token{
                .type = TokenType::Error,
                .lexeme = message,
                .sloc = m_start_sloc,
        };
    }

    auto check_keyword(std::string_view keyword, TokenType type) -> TokenType
    {
        if (get_lexeme() == keyword) {
            return type;
        }
        return TokenType::Identifier;
    }

    auto identifier_type() -> TokenType
    {
        using enum TokenType;
        auto lexeme = get_lexeme();
        switch (lexeme[0]) {
        case 'a': return check_keyword("and", And);
        case 'c': return check_keyword("class", Class);
        case 'e': return check_keyword("else", Else);
        case 'f':
            if (lexeme.length() > 1) {
                switch (lexeme[1]) {
                case 'a': return check_keyword("false", False);
                case 'o': return check_keyword("for", For);
                case 'u': return check_keyword("fun", Fun);
                default: return Identifier;
                }
            }
            return Identifier;
        case 'i': return check_keyword("if", If);
        case 'n': return check_keyword("nil", Nil);
        case 'o': return check_keyword("or", Or);
        case 'p': return check_keyword("print", Print);
        case 'r': return check_keyword("return", Return);
        case 's': return check_keyword("super", Super);
        case 't':
            if (lexeme.length() > 1) {
                switch (lexeme[1]) {
                case 'h': return check_keyword("this", This);
                case 'r': return check_keyword("true", True);
                default: return Identifier;
                }
            }
            return Identifier;
        case 'v': return check_keyword("var", Var);
        case 'w': return check_keyword("while", While);
        default: return Identifier;
        }
        std::unreachable();
    }

    auto string() -> Token
    {
        while (!is_at_end() && peek() != '"') {
            if (peek() == '\n') {
                m_sloc.line++;
                m_sloc.column = 0;
            }
            advance();
        }

        if (is_at_end()) {
            return error_token("Unterminated string.");
        }

        advance(); // consume closing quote
        return make_token(TokenType::String);
    }

    auto number() -> Token
    {
        while (!is_at_end() && is_digit(peek())) {
            advance();
        }

        if (m_current + 1 < m_source.length() && peek() == '.' && is_digit(peek_next())) {
            advance(); // consume dot
            while (!is_at_end() && is_digit(peek())) {
                advance();
            }
        }

        return make_token(TokenType::Number);
    }

    auto identifier() -> Token
    {
        while (!is_at_end() && (is_alpha(peek()) || is_digit(peek()) || peek() == '_')) {
            advance();
        }
        return make_token(identifier_type());
    }

private:
    std::string_view m_source;
    std::size_t m_start = 0;
    std::size_t m_current = 0;
    SourceLocation m_start_sloc = {.line = 1, .column = 1};
    SourceLocation m_sloc = {.line = 1, .column = 1};
};

auto make_scanner(std::string_view source) -> std::unique_ptr<IScanner>
{
    return std::make_unique<Scanner>(source);
}

} // namespace cpplox
