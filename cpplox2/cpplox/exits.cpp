export module cpplox2:exits;

import std;

namespace cpplox {

export enum class ExitCode : std::uint8_t {
    Ok = 0,
    // From sysexits(3)
    IncorrectUsage = 64,
    IncorrectInput = 65,
    SoftwareError = 70,
    IOError = 74,
};

export [[noreturn]] auto exit_program(ExitCode code) -> void { std::exit(static_cast<int>(code)); }

} // namespace cpplox
