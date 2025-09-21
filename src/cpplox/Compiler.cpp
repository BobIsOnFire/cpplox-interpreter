export module cpplox:Compiler;

import std;

import :Chunk;
import :Scanner;

namespace cpplox {

struct Parser
{
    Token current;
    Token previous;
    SourceLocation op_sloc;
    bool had_error = false;
    // TODO: prevents cascading errors, see error_at. Is there a better way to achieve this?
    bool panic_mode = false;
};

enum class Precedence : std::uint8_t
{
    None,
    Assignment, // =
    Or,         // or
    And,        // and
    Equality,   // == !=
    Comparison, // < > <= >=
    Term,       // + -
    Factor,     // * /
    Unary,      // ! -
    Call,       // . ()
    Primary,
};

struct ParseContext
{
    bool can_assign;
};

using ParseFn = void (*)(ParseContext);

struct ParseRule
{
    ParseFn prefix = nullptr;
    ParseFn infix = nullptr;
    Precedence precedence = Precedence::None;
};

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

struct Compiler
{
    Compiler * enclosing = nullptr;
    ObjFunction * function = nullptr;
    FunctionType type = FunctionType::Script;

    std::vector<Local> locals;
    std::vector<Upvalue> upvalues;
    int scope_depth = 0;
};

struct ClassCompiler
{
    std::string_view name;
    ClassCompiler * enclosing = nullptr;
    bool has_superclass = false;
};

// FIXME: get rid of singleton instance
Parser g_parser; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
Compiler * g_current_compiler = nullptr;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
ClassCompiler * g_current_class = nullptr;

export auto compile(std::string_view source) -> ObjFunction *;

} // namespace cpplox