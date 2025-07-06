export module cpplox2:Compiler;

import std;

import :Scanner;

namespace cpplox {

export auto compile(std::string_view source) -> void
{
    init_scanner(source);
    std::size_t line = 0;
    for (;;) {
        Token token = scan_token();
        if (token.sloc.line == line) {
            std::print("{:>4}:{:<4} ", '|', token.sloc.column);
        }
        else {
            std::print("{:>4}:{:<4} ", token.sloc.line, token.sloc.column);
        }
        std::println("{:16} '{}'", token.type, token.lexeme);

        if (token.type == TokenType::EndOfFile) {
            break;
        }
    }
}

} // namespace cpplox