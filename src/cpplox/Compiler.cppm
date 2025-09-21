export module cpplox:Compiler;

import std;

import :Chunk;
import :Scanner;

namespace cpplox {

struct Compiler
{
    struct Local
    {
        Token name;
        int depth = 0;
        bool is_captured = false;
    };

    struct Upvalue
    {
        Byte index;
        bool is_local;
    };

    enum class FunctionType : std::uint8_t
    {
        Function,
        Initializer,
        Method,
        Script,
    };

    Compiler * enclosing = nullptr;
    ObjFunction * function = nullptr;
    FunctionType type = FunctionType::Script;

    std::vector<Local> locals;
    std::vector<Upvalue> upvalues;
    int scope_depth = 0;
};

// FIXME: get rid of singleton instance
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
Compiler * g_current_compiler = nullptr;

export auto compile(std::string_view source) -> ObjFunction *;

} // namespace cpplox