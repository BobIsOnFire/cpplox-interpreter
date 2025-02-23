module cpplox;

import std;

// import :ASTSerializer;
import :Interpreter;
import :Parser;
import :Scanner;
import :exits;

namespace cpplox {

auto Lox::run(std::string source) -> ExitCode
{
    Scanner scanner(std::move(source));
    auto tokens = scanner.scan_tokens();
    if (had_error) {
        return ExitCode::IncorrectInput;
    }

    Parser parser(std::move(tokens));
    auto expression = parser.parse();

    if (had_error) {
        return ExitCode::IncorrectInput;
    }

    auto * interpreter = Interpreter::instance();
    interpreter->interpret(*expression);

    if (had_runtime_error) {
        return ExitCode::SoftwareError;
    }

    return ExitCode::Ok;
}

} // namespace cpplox
