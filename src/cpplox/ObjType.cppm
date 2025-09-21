export module cpplox:ObjType;

import std;

import :EnumFormatter;

namespace cpplox {

export enum class ObjType : std::uint8_t {
    BoundMethod,
    Class,
    Closure,
    Function,
    Instance,
    Native,
    String,
    Upvalue,
};

}

template <> struct std::formatter<cpplox::ObjType> : cpplox::EnumFormatter<cpplox::ObjType>
{
};
