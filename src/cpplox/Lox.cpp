export module cpplox:Lox;

import std;

import :exits;

namespace cpplox {

export class Lox
{
public:
    auto execute(std::span<std::string_view> args) -> ExitCode
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
            [[maybe_unused]] auto code = run(line);
        }
        std::println("\nexit");
        return ExitCode::Ok;
    }

private:
    [[nodiscard]] auto run(std::string source) -> ExitCode;
};

} // namespace cpplox
