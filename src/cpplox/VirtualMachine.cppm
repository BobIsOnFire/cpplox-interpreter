export module cpplox:VirtualMachine;

import std;

import :Chunk;
import :Value;

namespace cpplox {

// TODO: make this store error only, and use std::expected<std::monostate, InterpretError> for this
export enum class [[nodiscard]] InterpretResult : std::uint8_t {
    Ok,
    CompileError,
    RuntimeError,
};

export class IVirtualMachine
{
public:
    virtual ~IVirtualMachine() = default;
    virtual auto interpret(std::string_view source) -> InterpretResult = 0;
};

export using VirtualMachinePtr = std::unique_ptr<IVirtualMachine>;

export auto make_vm() -> VirtualMachinePtr;

} // namespace cpplox
