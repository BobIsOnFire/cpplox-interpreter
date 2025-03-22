module cpplox;

import std;

import :Diagnostics;
import :Interpreter;
import :Parser;
import :Scanner;
// import :StmtSerializer;
import :exits;

namespace cpplox {

auto Lox::run(std::string source) -> ExitCode
{
    Scanner scanner(std::move(source));
    auto tokens = scanner.scan_tokens();
    if (Diagnostics::instance()->has_errors()) {
        return ExitCode::IncorrectInput;
    }

    Parser parser(tokens);
    auto statements = parser.parse();

    if (Diagnostics::instance()->has_errors()) {
        return ExitCode::IncorrectInput;
    }

    // for (const auto & stmt : statements) {
    //     YAML::Emitter emitter;
    //     std::visit(StmtSerializer{emitter}, *stmt);
    //     std::println("---\n{}", emitter.c_str());
    // }

    auto * interpreter = Interpreter::instance();
    interpreter->interpret(statements);

    if (Diagnostics::instance()->has_runtime_errors()) {
        return ExitCode::SoftwareError;
    }

    return ExitCode::Ok;
}

} // namespace cpplox
