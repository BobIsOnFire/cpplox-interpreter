module cpplox;

import std;

import :ASTSerializer;
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

    ASTSerializer serializer;
    std::visit(serializer, *expression);

    std::println("{}", serializer.emitter().c_str());

    return ExitCode::Ok;
}

} // namespace cpplox
