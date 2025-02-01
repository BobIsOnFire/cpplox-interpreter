module;

#include <cstdio> // for stderr

export module cpplox.Lox;

import cpplox.Scanner;
import cpplox.exits;

import std;

namespace cpplox {

export class Lox
{
public:
    auto execute(const std::vector<std::string_view> & args) -> ExitCode
    {
        if (args.size() > 1) {
            std::println(stderr, "Usage: cpplox [script]");
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

    auto error(int line, std::string_view message) -> void { report(line, "", message); }

private:
    auto run(std::string source) -> ExitCode
    {
        Scanner scanner(std::move(source));

        for (auto token : scanner.scan_tokens()) {
            std::println("Token: {}", token);
        }

        if (had_error) {
            return ExitCode::IncorrectInput;
        }

        return ExitCode::Ok;
    }

    auto report(int line, std::string_view where, std::string_view message) -> void
    {
        std::println(stderr, "[line {}] Error{}: {}", line, where, message);
        had_error = true;
    }

    bool had_error = false;
};

} // namespace cpplox
