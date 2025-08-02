module;

#include <cassert>

export module cpplox2:Compiler;

import std;

import :Chunk;
import :Debug;
import :Object;
import :Scanner;

import magic_enum;

namespace cpplox {

namespace {
constexpr const bool DEBUG_PRINT_CODE = true;
}

struct Parser
{
    Token current;
    Token previous;
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
};

struct Compiler
{
    std::vector<Local> locals;
    int scope_depth = 0;
};

namespace {

// FIXME: get rid of singleton instance
Parser g_parser;               // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
Compiler * g_current_compiler; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// *** Token Parser ***

auto error_at(const Token & token, std::string_view message) -> void
{
    if (g_parser.panic_mode) {
        return;
    }
    g_parser.panic_mode = true;

    std::print(std::cerr, "[{}:{}] Error", token.sloc.line, token.sloc.column);

    if (token.type == TokenType::EndOfFile) {
        std::print(std::cerr, " at end");
    }
    else {
        std::print(std::cerr, " at '{}'", token.lexeme);
    }

    std::println(": {}", message);

    g_parser.had_error = true;
}

auto error(std::string_view message) -> void { error_at(g_parser.previous, message); }
auto error_at_current(std::string_view message) -> void { error_at(g_parser.current, message); }

auto advance() -> void
{
    g_parser.previous = g_parser.current;

    for (;;) {
        g_parser.current = scan_token();
        if (g_parser.current.type != TokenType::Error) {
            break;
        }
        error_at_current(g_parser.current.lexeme);
    }
}

auto consume(TokenType type, std::string_view message) -> void
{
    if (g_parser.current.type == type) {
        advance();
        return;
    }

    error_at_current(message);
}

auto check(TokenType type) -> bool { return g_parser.current.type == type; }

auto match(TokenType type) -> bool
{
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

auto synchronize() -> void
{
    using enum TokenType;

    g_parser.panic_mode = false;
    while (g_parser.current.type != EndOfFile) {
        if (g_parser.previous.type == Semicolon) {
            return;
        }
        switch (g_parser.current.type) {
        case Class:
        case Fun:
        case Var:
        case For:
        case If:
        case While:
        case Print:
        case Return: return;

        default: // do nothing
        }

        advance();
    }
}

// *** Byte Code Emitter ***

// FIXME: get rid of singleton instance
Chunk * g_compiling_chunk; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

auto current_chunk() -> Chunk & { return *g_compiling_chunk; }

auto emit_byte(Byte byte) -> void { write_chunk(current_chunk(), byte, g_parser.previous.sloc); }
auto emit_byte(OpCode op) -> void { write_chunk(current_chunk(), op, g_parser.previous.sloc); }

template <typename ByteT, typename... Bytes> auto emit_bytes(ByteT byte, Bytes... bytes) -> void
{
    emit_byte(byte);
    if constexpr (sizeof...(bytes) > 0) {
        emit_bytes(bytes...);
    }
}

auto emit_loop(std::size_t start) -> void
{
    emit_byte(OpCode::Loop);

    std::size_t offset = current_chunk().code.size() - start + 2;
    if (offset > DOUBLE_BYTE_MAX) {
        error("Loop body too large.");
    }

    emit_byte(static_cast<Byte>((offset >> BYTE_DIGITS) & BYTE_MAX));
    emit_byte(static_cast<Byte>(offset & BYTE_MAX));
}

auto emit_jump(OpCode instruction) -> std::size_t
{
    emit_bytes(instruction, BYTE_MAX, BYTE_MAX);

    return current_chunk().code.size() - 2;
}

auto patch_jump(std::size_t offset) -> void
{
    std::size_t jump_length = current_chunk().code.size() - offset - 2;

    if (jump_length > DOUBLE_BYTE_MAX) {
        error("Too much code to jump over.");
    }

    current_chunk().code[offset] = (jump_length >> BYTE_DIGITS) & BYTE_MAX;
    current_chunk().code[offset + 1] = jump_length & BYTE_MAX;
}

auto make_constant(Value value) -> Byte
{
    std::size_t c = add_constant(current_chunk(), value);
    if (c >= BYTE_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return static_cast<Byte>(c);
}

auto emit_constant(Value value) -> void { emit_bytes(OpCode::Constant, make_constant(value)); }

auto emit_return() -> void { emit_byte(OpCode::Return); }

auto end_compiler() -> void
{
    emit_return();
    if constexpr (DEBUG_PRINT_CODE) {
        if (!g_parser.had_error) {
            disassemble_chunk(current_chunk(), "code");
        }
    }
}

auto is_scope_local() -> bool { return g_current_compiler->scope_depth > 0; }

auto begin_scope() -> void { g_current_compiler->scope_depth++; }

auto end_scope() -> void
{
    g_current_compiler->scope_depth--;

    while (g_current_compiler->locals.size() > 0
           && g_current_compiler->locals.back().depth > g_current_compiler->scope_depth) {
        g_current_compiler->locals.pop_back();
        emit_byte(OpCode::Pop);
    }
}

auto identifier_constant(const Token & name) -> Byte
{
    return make_constant(Value::string(std::string{name.lexeme}));
}

auto add_local(const Token & name) -> void
{
    if (g_current_compiler->locals.size() > BYTE_MAX) {
        error("Too many local variables in function.");
        return;
    }

    g_current_compiler->locals.emplace_back(name, -1);
}

auto resolve_local(Compiler * compiler, const Token & name) -> std::optional<std::size_t>
{
    // FIXME: UHH why `vector | views::enumerate | views::reverse` doesn't work??? libstdc++ wtf???
    for (const auto & [idx, local] :
         std::ranges::reverse_view{std::ranges::enumerate_view{compiler->locals}}) {
        if (local.name.lexeme == name.lexeme) {
            if (local.depth == -1) {
                error("Can't read local variable in its own initializer.");
            }
            return idx;
        }
    }
    return std::nullopt;
}

auto mark_initialized() -> void
{
    g_current_compiler->locals.back().depth = g_current_compiler->scope_depth;
}

auto declare_variable() -> void
{
    if (!is_scope_local()) {
        return;
    }

    const Token & name = g_parser.previous;
    for (const auto & local : std::ranges::reverse_view{g_current_compiler->locals}) {
        if (local.depth != -1 && local.depth < g_current_compiler->scope_depth) {
            break;
        }

        if (local.name.lexeme == name.lexeme) {
            error("Already a variable with this name in this scope.");
        }
    }

    add_local(g_parser.previous);
}

auto parse_variable(std::string_view error_message) -> Byte
{
    consume(TokenType::Identifier, error_message);
    declare_variable();

    if (is_scope_local()) {
        return 0;
    }

    return identifier_constant(g_parser.previous);
}

auto define_variable(Byte global) -> void
{
    if (is_scope_local()) {
        mark_initialized();
        return;
    }
    emit_bytes(OpCode::DefineGlobal, global);
}

// *** Expression Parser ***

auto parse_precedence(Precedence precedence) -> void;
auto get_rule(TokenType type) -> const ParseRule &;

auto next_precedence(Precedence precedence) -> Precedence
{
    return static_cast<Precedence>(static_cast<std::uint8_t>(precedence) + 1);
}

auto expression() -> void { parse_precedence(Precedence::Assignment); }

auto number(ParseContext /* ctx */) -> void
{
    double value = 0;

    [[maybe_unused]] auto result = std::from_chars(
            g_parser.previous.lexeme.begin(), g_parser.previous.lexeme.end(), value
    );
    assert((result.ec == std::errc{}) && "Cannot parse Number token provided by Scanner");

    emit_constant(Value::number(value));
}

auto grouping(ParseContext /* ctx */) -> void
{
    expression();
    consume(TokenType::RightParenthesis, "Expect ')' after expression.");
}

auto unary(ParseContext /* ctx */) -> void
{
    TokenType operator_type = g_parser.previous.type;

    parse_precedence(Precedence::Unary);

    switch (operator_type) {
    case TokenType::Bang: emit_byte(OpCode::Not); break;
    case TokenType::Minus: emit_byte(OpCode::Negate); break;
    default: error("Unknown unary operand.");
    }
}

auto binary(ParseContext /* ctx */) -> void
{
    TokenType operator_type = g_parser.previous.type;
    parse_precedence(next_precedence(get_rule(operator_type).precedence));

    switch (operator_type) {
    case TokenType::BangEqual: emit_bytes(OpCode::Equal, OpCode::Not); break;
    case TokenType::EqualEqual: emit_byte(OpCode::Equal); break;

    case TokenType::Greater: emit_byte(OpCode::Greater); break;
    case TokenType::GreaterEqual: emit_bytes(OpCode::Less, OpCode::Not); break;
    case TokenType::Less: emit_byte(OpCode::Less); break;
    case TokenType::LessEqual: emit_bytes(OpCode::Greater, OpCode::Not); break;

    case TokenType::Plus: emit_byte(OpCode::Add); break;
    case TokenType::Minus: emit_byte(OpCode::Substract); break;
    case TokenType::Star: emit_byte(OpCode::Multiply); break;
    case TokenType::Slash: emit_byte(OpCode::Divide); break;
    default: error("Unknown binary operand.");
    }
}

auto literal(ParseContext /* ctx */) -> void
{
    switch (g_parser.previous.type) {
    case TokenType::False: emit_byte(OpCode::False); break;
    case TokenType::True: emit_byte(OpCode::True); break;
    case TokenType::Nil: emit_byte(OpCode::Nil); break;
    default: error("Unknown literal.");
    }
}

auto string(ParseContext /* ctx */) -> void
{
    auto lexeme = g_parser.previous.lexeme;
    emit_constant(Value::string(std::string{lexeme.substr(1, lexeme.length() - 2)}));
}

auto and_ex(ParseContext /* ctx */) -> void
{
    std::size_t end_jump = emit_jump(OpCode::JumpIfFalse);

    emit_byte(OpCode::Pop);
    parse_precedence(Precedence::And);

    patch_jump(end_jump);
}

auto or_ex(ParseContext /* ctx */) -> void
{
    std::size_t else_jump = emit_jump(OpCode::JumpIfFalse);
    std::size_t end_jump = emit_jump(OpCode::Jump);

    patch_jump(else_jump);
    emit_byte(OpCode::Pop);

    parse_precedence(Precedence::Or);
    patch_jump(end_jump);
}

auto named_variable(const Token & name, ParseContext ctx) -> void
{
    OpCode get_op = OpCode::GetGlobal;
    OpCode set_op = OpCode::SetGlobal;
    Byte arg = 0;

    auto local_pos = resolve_local(g_current_compiler, name);
    if (local_pos.has_value()) {
        get_op = OpCode::GetLocal;
        set_op = OpCode::SetLocal;
        arg = static_cast<Byte>(local_pos.value());
    }
    else {
        get_op = OpCode::GetGlobal;
        set_op = OpCode::SetGlobal;
        arg = identifier_constant(name);
    }

    if (ctx.can_assign && match(TokenType::Equal)) {
        expression();
        emit_bytes(set_op, arg);
    }
    else {
        emit_bytes(get_op, arg);
    }
}

auto variable(ParseContext ctx) -> void { named_variable(g_parser.previous, ctx); }

// *** Statement Parser ***

// It's a recursive descent parser, duh!
// NOLINTBEGIN(misc-no-recursion)
auto statement() -> void;
auto declaration() -> void;

auto var_declaration() -> void
{
    Byte global = parse_variable("Expect variable name.");

    if (match(TokenType::Equal)) {
        expression();
    }
    else {
        emit_byte(OpCode::Nil);
    }
    consume(TokenType::Semicolon, "Expect ';' after variable declaration.");

    define_variable(global);
}

auto print_statement() -> void
{
    expression();
    consume(TokenType::Semicolon, "Expect ';' after value.");
    emit_byte(OpCode::Print);
}

auto expression_statement() -> void
{
    expression();
    consume(TokenType::Semicolon, "Expect ';' after expression.");
    emit_byte(OpCode::Pop);
}

auto block() -> void
{
    while (!check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
        declaration();
    }

    consume(TokenType::RightBrace, "Expect '}' after block.");
}

auto if_statement() -> void
{
    consume(TokenType::LeftParenthesis, "Expect '(' after 'if'.");
    expression();
    consume(TokenType::RightParenthesis, "Expect ')' after condition.");

    std::size_t then_jump = emit_jump(OpCode::JumpIfFalse);
    emit_byte(OpCode::Pop);

    statement();

    std::size_t else_jump = emit_jump(OpCode::Jump);

    patch_jump(then_jump);
    emit_byte(OpCode::Pop);

    if (match(TokenType::Else)) {
        statement();
    }

    patch_jump(else_jump);
}

auto while_statement() -> void
{
    std::size_t loop_start = current_chunk().code.size();

    consume(TokenType::LeftParenthesis, "Expect '(' after 'while'.");
    expression();
    consume(TokenType::RightParenthesis, "Expect ')' after condition.");

    std::size_t exit_jump = emit_jump(OpCode::JumpIfFalse);
    emit_byte(OpCode::Pop);
    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OpCode::Pop);
}

auto for_statement() -> void
{
    // Completely atrocious -- so many weird jumps! TODO - should revisit AST creation and
    // double-pass approach.

    begin_scope();

    consume(TokenType::LeftParenthesis, "Expect '(' after 'for'.");
    if (match(TokenType::Semicolon)) {
        // No initializer
    }
    else if (match(TokenType::Var)) {
        var_declaration();
    }
    else {
        expression_statement();
    }

    std::size_t loop_start = current_chunk().code.size();

    std::optional<std::size_t> exit_jump;
    if (!match(TokenType::Semicolon)) {
        expression();
        consume(TokenType::Semicolon, "Expect ';' after loop condition.");

        exit_jump = emit_jump(OpCode::JumpIfFalse);
        emit_byte(OpCode::Pop);
    }

    if (!match(TokenType::RightParenthesis)) {
        std::size_t body_jump = emit_jump(OpCode::Jump);
        std::size_t increment_start = current_chunk().code.size();

        expression();

        emit_byte(OpCode::Pop);
        consume(TokenType::RightParenthesis, "Expect ')' after for clauses.");

        emit_loop(loop_start);
        loop_start = increment_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump.has_value()) {
        patch_jump(exit_jump.value());
        emit_byte(OpCode::Pop);
    }

    end_scope();
}

auto statement() -> void
{
    if (match(TokenType::Print)) {
        print_statement();
    }
    else if (match(TokenType::If)) {
        if_statement();
    }
    else if (match(TokenType::While)) {
        while_statement();
    }
    else if (match(TokenType::For)) {
        for_statement();
    }
    else if (match(TokenType::LeftBrace)) {
        begin_scope();
        block();
        end_scope();
    }
    else {
        expression_statement();
    }
}

auto declaration() -> void
{
    if (match(TokenType::Var)) {
        var_declaration();
    }
    else {
        statement();
    }
    if (g_parser.panic_mode) {
        synchronize();
    }
}
// NOLINTEND(misc-no-recursion)

template <typename T> struct array_size;

template <typename T, std::size_t N> struct array_size<const std::array<T, N>>
{
    constexpr static std::size_t value = N;
};

// oh my god holy shit
consteval auto generate_rule_table() noexcept
{
    using enum Precedence;
    std::array<ParseRule, magic_enum::enum_values<TokenType>().size()> rules;

    // clang-format off
    const auto to_idx = [](TokenType typ) { return static_cast<std::size_t>(typ); };
    rules[to_idx(TokenType::And)]             = {.prefix = nullptr,  .infix = and_ex,  .precedence = And};
    rules[to_idx(TokenType::Bang)]            = {.prefix = unary,    .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::BangEqual)]       = {.prefix = nullptr,  .infix = binary,  .precedence = Equality};
    rules[to_idx(TokenType::EqualEqual)]      = {.prefix = nullptr,  .infix = binary,  .precedence = Equality};
    rules[to_idx(TokenType::False)]           = {.prefix = literal,  .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::Greater)]         = {.prefix = nullptr,  .infix = binary,  .precedence = Comparison};
    rules[to_idx(TokenType::GreaterEqual)]    = {.prefix = nullptr,  .infix = binary,  .precedence = Comparison};
    rules[to_idx(TokenType::Identifier)]      = {.prefix = variable, .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::LeftParenthesis)] = {.prefix = grouping, .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::Less)]            = {.prefix = nullptr,  .infix = binary,  .precedence = Comparison};
    rules[to_idx(TokenType::LessEqual)]       = {.prefix = nullptr,  .infix = binary,  .precedence = Comparison};
    rules[to_idx(TokenType::Minus)]           = {.prefix = unary,    .infix = binary,  .precedence = Term};
    rules[to_idx(TokenType::Nil)]             = {.prefix = literal,  .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::Number)]          = {.prefix = number,   .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::Or)]              = {.prefix = nullptr,  .infix = or_ex,   .precedence = Or};
    rules[to_idx(TokenType::Plus)]            = {.prefix = nullptr,  .infix = binary,  .precedence = Term};
    rules[to_idx(TokenType::Slash)]           = {.prefix = nullptr,  .infix = binary,  .precedence = Factor};
    rules[to_idx(TokenType::Star)]            = {.prefix = nullptr,  .infix = binary,  .precedence = Factor};
    rules[to_idx(TokenType::String)]          = {.prefix = string,   .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::True)]            = {.prefix = literal,  .infix = nullptr, .precedence = None};
    // clang-format on

    return rules;
}

constinit const auto g_rule_table = generate_rule_table();

auto get_rule(TokenType type) -> const ParseRule &
{
    // Accessing a table via TokenType is safe, it has elements precisely for each TokenType value
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    return g_rule_table[static_cast<std::size_t>(type)];
}

auto parse_precedence(Precedence precedence) -> void
{
    advance();
    auto prefix_rule = get_rule(g_parser.previous.type).prefix;
    if (prefix_rule == nullptr) {
        error("Expect expression.");
        return;
    }

    ParseContext ctx{.can_assign = precedence <= Precedence::Assignment};
    prefix_rule(ctx);

    while (precedence <= get_rule(g_parser.current.type).precedence) {
        advance();
        auto infix_rule = get_rule(g_parser.previous.type).infix;
        assert(infix_rule != nullptr);
        infix_rule(ctx);
    }

    if (ctx.can_assign && match(TokenType::Equal)) {
        error("Invalid assignment target.");
    }
}

} // namespace

// TODO: should denote failure, replace with std::expected
export auto compile(std::string_view source) -> std::optional<Chunk>
{
    Compiler compiler;
    g_current_compiler = &compiler;

    Chunk chunk;
    g_compiling_chunk = &chunk;

    init_scanner(source);
    advance();

    while (!match(TokenType::EndOfFile)) {
        declaration();
    }

    end_compiler();

    if (g_parser.had_error) {
        return std::nullopt;
    }
    return chunk;
}

} // namespace cpplox