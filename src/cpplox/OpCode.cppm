export module cpplox:OpCode;

import std;

import :EnumFormatter;

namespace cpplox {

export using Byte = std::uint8_t;
export using DoubleByte = std::uint16_t;

static_assert(sizeof(Byte) * 2 == sizeof(DoubleByte));

constexpr const unsigned int BYTE_DIGITS = std::numeric_limits<Byte>::digits;
constexpr const Byte BYTE_MAX = std::numeric_limits<Byte>::max();
constexpr const DoubleByte DOUBLE_BYTE_MAX = std::numeric_limits<DoubleByte>::max();

export enum class OpCode : Byte {
    // Values
    Constant,
    Nil,
    True,
    False,
    // Value manipulators
    Pop,
    DefineGlobalConst,
    DefineGlobalVar,
    GetGlobal,
    GetLocal,
    GetProperty,
    GetSuper,
    GetUpvalue,
    SetGlobal,
    SetLocal,
    SetProperty,
    SetUpvalue,
    // Comparison ops
    Equal,
    Greater,
    Less,
    // Binary ops
    Add,
    Subtract,
    Multiply,
    Divide,
    // Unary ops
    Not,
    Negate,
    // Aux
    Print,
    Jump,
    JumpIfFalse,
    JumpIfFalseAndPop,
    Loop,
    Call,
    Invoke,
    SuperInvoke,
    Closure,
    CloseUpvalue,
    Return,
    Class,
    Inherit,
    Method,
};

} // namespace cpplox

template <>
struct std::formatter<cpplox::OpCode> : cpplox::EnumFormatter<cpplox::OpCode>
{
};
