import std;
import cpplox;

auto main(int argc, char * argv[]) -> int
{
    auto args = std::span(argv, static_cast<std::size_t>(argc))
            | std::views::drop(1) // drop argv[0], it's executable name
            | std::views::transform([](char const * arg) { return std::string_view{arg}; })
            | std::ranges::to<std::vector>();

    cpplox::Lox lox;
    cpplox::exit_program(lox.execute(args));
}
