export module cpplox:Lox;

import std;

import :exits;

// FIXME: Lox is external interpreter class, called outside of implementation, but it provides
// error() method for library impl to call into. Move error() into a subclass.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wimport-implementation-partition-unit-in-interface-unit"
import :Token;
#pragma clang diagnostic pop

namespace cpplox {

export class Lox
{
public:
    static auto instance() -> Lox
    {
        static Lox lox;
        return lox;
    }

    auto execute(const std::vector<std::string_view> & args) -> ExitCode
    {
        if (args.size() > 1) {
            std::println(std::cerr, "Usage: cpplox [script]");
            return cpplox::ExitCode::IncorrectUsage;
        }
        if (args.size() == 1) {
            return run_file(args[0]);
        }

        return run_prompt();
    }

    auto run_file(std::filesystem::path filename) -> ExitCode
    {
        std::ifstream script(filename);
        std::stringstream buffer;
        buffer << script.rdbuf();
        script.close();

        return run(buffer.str());
    }

    auto run_prompt() -> ExitCode
    {
        for (std::string line; std::print("> "), std::getline(std::cin, line);) {
            auto code = run(line);
            if (code != ExitCode::Ok) {
                std::println("\nexit");
                return code;
            }
        }
        std::println("\nexit");
        return ExitCode::Ok;
    }

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

private:
    Lox() = default;

    // Some internal modules called by run() (like Scanner) need to call into Lox::error() and
    // Lox::report() methods, which forces us to move run() implementation into the implementation
    // unit (see LoxImpl.cpp)
    auto run(std::string source) -> ExitCode;

    auto report(std::size_t line, std::string_view where, std::string_view message) -> void
    {
        std::println(std::cerr, "[line {}] Error{}: {}", line, where, message);
        had_error = true;
    }

    bool had_error = false;
};

} // namespace cpplox
