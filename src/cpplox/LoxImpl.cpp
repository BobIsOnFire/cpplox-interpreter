module cpplox;

import :Scanner;
import :exits;

import std;

namespace cpplox {

auto Lox::run(std::string source) -> ExitCode
{
    Scanner scanner(std::move(source));

    for (auto token : scanner.scan_tokens()) {
        std::println("{}", token);
    }

    if (had_error) {
        return ExitCode::IncorrectInput;
    }

    return ExitCode::Ok;
}

} // namespace cpplox
