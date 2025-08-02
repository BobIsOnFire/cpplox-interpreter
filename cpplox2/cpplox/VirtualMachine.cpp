export module cpplox2:VirtualMachine;

import std;

import :Chunk;
import :Value;

namespace cpplox {

export struct CallFrame
{
    ObjFunction * function;
    const Byte * ip;
    Value * slots; // TODO: std::span? or store offset?
};

export struct VirtualMachine
{
    std::vector<CallFrame> frames;
    std::vector<Value> stack;
    std::vector<Obj *> objects;
    std::unordered_map<std::string, Value> globals;
};

// TODO: make this store error only, and use std::expected<std::monostate, InterpretError> for this
export enum class [[nodiscard]] InterpretResult : std::uint8_t {
    Ok,
    CompileError,
    RuntimeError,
};

// FIXME: get rid of singleton instance
VirtualMachine g_vm; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// FIXME: Should be done by constructor/desctructor, but should get rid of global object first
export auto init_vm() -> void;
export auto free_vm() -> void;

export auto interpret(std::string_view source) -> InterpretResult;

} // namespace cpplox
