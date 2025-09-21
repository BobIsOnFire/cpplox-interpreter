import std;
import cpplox;

namespace {

auto repl() -> void
{
    cpplox::init_vm();
    for (std::string line; std::print("> "), std::getline(std::cin, line);) {
        [[maybe_unused]] auto result = cpplox::interpret(line);
    }
    std::println("\nexit");
    cpplox::free_vm();
}

auto run_file(const std::filesystem::path & filename) -> void
{
    std::ifstream script(filename);
    if (!script.is_open()) {
        std::println("Failed to open {}", filename.native());
        cpplox::exit_program(cpplox::ExitCode::IOError);
    }

    std::stringstream buffer;
    buffer << script.rdbuf();
    script.close();

    cpplox::init_vm();
    auto result = cpplox::interpret(buffer.str());
    cpplox::free_vm();

    if (result == cpplox::InterpretResult::CompileError) {
        cpplox::exit_program(cpplox::ExitCode::IncorrectInput);
    }
    if (result == cpplox::InterpretResult::RuntimeError) {
        cpplox::exit_program(cpplox::ExitCode::SoftwareError);
    }
}

} // namespace

auto main(int argc, char ** argv) -> int
{
    auto args = std::span(argv, static_cast<std::size_t>(argc))
            | std::views::drop(1) // drop argv[0], it's executable name
            | std::views::transform([](char const * arg) { return std::string_view{arg}; })
            | std::ranges::to<std::vector>();

    if (args.size() == 0) {
        repl();
    }
    else if (args.size() == 1) {
        run_file(args[0]);
    }
    else {
        std::println(std::cerr, "Usage: cpplox [path]");
        cpplox::exit_program(cpplox::ExitCode::IncorrectUsage);
    }
}
