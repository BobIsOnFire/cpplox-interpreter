module cpplox;

import std;

// import :ASTSerializer;
import :Diagnostics;
import :Interpreter;
import :Parser;
import :Scanner;
import :exits;

namespace cpplox {

auto Lox::run(std::string source) -> ExitCode
{
    Scanner scanner(std::move(source));
    auto tokens = scanner.scan_tokens();
    if (Diagnostics::instance()->has_errors()) {
        return ExitCode::IncorrectInput;
    }

    Parser parser(std::move(tokens));
    auto expression = parser.parse();

    if (Diagnostics::instance()->has_errors()) {
        return ExitCode::IncorrectInput;
    }

    auto * interpreter = Interpreter::instance();
    interpreter->interpret(*expression);

    if (Diagnostics::instance()->has_runtime_errors()) {
        return ExitCode::SoftwareError;
    }

    return ExitCode::Ok;
}

} // namespace cpplox
