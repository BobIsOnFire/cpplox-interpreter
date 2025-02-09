#include <vector> // WTF "vector must be defined before used"

import cpplox;

import std;

auto main(int argc, char * argv[]) -> int
{
    auto args = std::span(argv, argc) | std::views::drop(1) // drop argv[0], it's executable name
            | std::views::transform([](char const * arg) { return std::string_view{arg}; })
            | std::ranges::to<std::vector>();

    auto lox = cpplox::Lox::instance();
    cpplox::exit_program(lox.execute(args));
}
