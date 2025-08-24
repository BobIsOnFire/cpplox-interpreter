module;

#include <cassert>

export module cpplox2:CompilerImpl;

import std;

import :Chunk;
import :Compiler;
import :Debug;
import :Object;
import :Scanner;

import magic_enum;

namespace cpplox {

namespace {
constexpr const std::size_t MAX_ARITY = 255;
constexpr const bool DEBUG_PRINT_CODE = true;
} // namespace

namespace {

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

auto current_chunk() -> Chunk & { return g_current_compiler->function->get_chunk(); }

auto emit_byte(Byte byte) -> void { write_chunk(current_chunk(), byte, g_parser.op_sloc); }
auto emit_byte(OpCode op) -> void { write_chunk(current_chunk(), op, g_parser.op_sloc); }

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

auto emit_return() -> void
{
    if (g_current_compiler->type == FunctionType::Initializer) {
        emit_bytes(OpCode::GetLocal, static_cast<Byte>(0));
    }
    else {
        emit_byte(OpCode::Nil);
    }
    emit_byte(OpCode::Return);
}

auto init_compiler(Compiler & compiler, FunctionType type) -> void
{
    compiler.enclosing = g_current_compiler;
    compiler.function = ObjFunction::create(
            std::string{type == FunctionType::Script ? "" : g_parser.previous.lexeme}
    );
    compiler.type = type;
    compiler.locals.push_back({
            .name = {
                .type = TokenType::EndOfFile,
                .lexeme = type == FunctionType::Function ? "" : "this",
                .sloc = {},
            },
            .depth = 0,
            .is_captured = false,
    });

    g_current_compiler = &compiler;
}

auto end_compiler() -> ObjFunction *
{
    emit_return();
    auto * function = g_current_compiler->function;
    if constexpr (DEBUG_PRINT_CODE) {
        if (!g_parser.had_error) {
            auto name = function->get_name();
            disassemble_chunk(current_chunk(), name.empty() ? "<script>" : name);
        }
    }
    g_current_compiler = g_current_compiler->enclosing;
    return function;
}

auto is_scope_local() -> bool { return g_current_compiler->scope_depth > 0; }

auto begin_scope() -> void { g_current_compiler->scope_depth++; }

auto end_scope() -> void
{
    g_current_compiler->scope_depth--;

    while (g_current_compiler->locals.size() > 0
           && g_current_compiler->locals.back().depth > g_current_compiler->scope_depth) {
        if (g_current_compiler->locals.back().is_captured) {
            emit_byte(OpCode::CloseUpvalue);
        }
        else {
            emit_byte(OpCode::Pop);
        }
        g_current_compiler->locals.pop_back();
    }
}

auto identifier_constant(const Token & name) -> Byte
{
    return make_constant(Value::string(std::string{name.lexeme}));
}

auto synthetic_token(std::string_view name) -> Token
{
    return {.type = TokenType::Identifier, .lexeme = name, .sloc = g_parser.previous.sloc};
}

auto add_local(const Token & name) -> void
{
    if (g_current_compiler->locals.size() > BYTE_MAX) {
        error("Too many local variables in function.");
        return;
    }

    g_current_compiler->locals.push_back({
            .name = name,
            .depth = -1,
            .is_captured = false,
    });
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

auto add_upvalue(Compiler * compiler, Byte index, bool is_local) -> std::size_t
{
    for (const auto & [idx, upvalue] : std::ranges::enumerate_view(compiler->upvalues)) {
        if (upvalue.index == index && upvalue.is_local == is_local) {
            return static_cast<std::size_t>(idx);
        }
    }

    if (compiler->upvalues.size() >= BYTE_MAX) {
        error("Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues.push_back({.index = index, .is_local = is_local});
    return compiler->function->upvalue_count()++;
}

// NOLINTNEXTLINE(misc-no-recursion)
auto resolve_upvalue(Compiler * compiler, const Token & name) -> std::optional<std::size_t>
{
    if (compiler->enclosing == nullptr) {
        return std::nullopt;
    }

    auto local = resolve_local(compiler->enclosing, name);
    if (local.has_value()) {
        compiler->enclosing->locals[local.value()].is_captured = true;
        return add_upvalue(compiler, static_cast<Byte>(local.value()), /* is_local = */ true);
    }

    auto upvalue = resolve_upvalue(compiler->enclosing, name);
    if (upvalue.has_value()) {
        return add_upvalue(compiler, static_cast<Byte>(upvalue.value()), /* is_local = */ false);
    }

    return std::nullopt;
}

auto mark_initialized() -> void
{
    if (g_current_compiler->scope_depth == 0) {
        return;
    }
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

    if (auto local_pos = resolve_local(g_current_compiler, name); local_pos.has_value()) {
        get_op = OpCode::GetLocal;
        set_op = OpCode::SetLocal;
        arg = static_cast<Byte>(local_pos.value());
    }
    else if (auto upvalue_pos = resolve_upvalue(g_current_compiler, name);
             upvalue_pos.has_value()) {
        get_op = OpCode::GetUpvalue;
        set_op = OpCode::SetUpvalue;
        arg = static_cast<Byte>(upvalue_pos.value());
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

auto this_ex(ParseContext /* ctx */) -> void
{
    if (g_current_class == nullptr) {
        error("Can't use 'this' outside of a class.");
    }

    variable({.can_assign = false});
}

auto argument_list() -> Byte
{
    Byte arg_count = 0;
    if (!check(TokenType::RightParenthesis)) {
        do {
            expression();
            arg_count++;
            if (arg_count > BYTE_MAX) {
                error("Cannot have more than 255 arguments.");
            }
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParenthesis, "Expect ')' after arguments.");
    return arg_count;
}

auto call(ParseContext /* ctx */) -> void
{
    Byte arg_count = argument_list();
    emit_bytes(OpCode::Call, arg_count);
}

auto super_ex(ParseContext /* ctx */) -> void
{
    if (g_current_class == nullptr) {
        error("Can't use 'super' outside of a class.");
    }
    else if (!g_current_class->has_superclass) {
        error("Can't use 'super' in a class with no superclass.");
    }

    consume(TokenType::Dot, "Expect '.' after 'super'.");

    SourceLocation prev_op_sloc = g_parser.op_sloc;
    g_parser.op_sloc = g_parser.current.sloc;

    consume(TokenType::Identifier, "Expect superclass method name.");

    Byte name = identifier_constant(g_parser.previous);

    named_variable(synthetic_token("this"), {.can_assign = false});

    if (match(TokenType::LeftParenthesis)) {
        Byte arg_count = argument_list();
        named_variable(synthetic_token("super"), {.can_assign = false});
        emit_bytes(OpCode::SuperInvoke, name);
        emit_byte(arg_count);
    }
    else {
        named_variable(synthetic_token("super"), {.can_assign = false});
        emit_bytes(OpCode::GetSuper, name);
    }

    g_parser.op_sloc = prev_op_sloc;
}

auto dot(ParseContext ctx) -> void
{
    SourceLocation prev_op_sloc = g_parser.op_sloc;
    g_parser.op_sloc = g_parser.current.sloc;

    consume(TokenType::Identifier, "Expect property name after '.'.");
    Byte name = identifier_constant(g_parser.previous);

    if (ctx.can_assign && match(TokenType::Equal)) {
        expression();
        emit_bytes(OpCode::SetProperty, name);
    }
    else if (match(TokenType::LeftParenthesis)) {
        Byte arg_count = argument_list();
        emit_bytes(OpCode::Invoke, name);
        emit_byte(arg_count);
    }
    else {
        emit_bytes(OpCode::GetProperty, name);
    }

    g_parser.op_sloc = prev_op_sloc;
}

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

auto return_statement() -> void
{
    if (g_current_compiler->type == FunctionType::Script) {
        error("Can't return from top-level code.");
    }

    if (match(TokenType::Semicolon)) {
        emit_return();
    }
    else {
        if (g_current_compiler->type == FunctionType::Initializer) {
            error("Can't return a value from initializer.");
        }

        expression();
        consume(TokenType::Semicolon, "Expect ';' after return value.");
        emit_byte(OpCode::Return);
    }
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

auto function(FunctionType type) -> void
{
    Compiler compiler;
    init_compiler(compiler, type);

    begin_scope();

    consume(TokenType::LeftParenthesis, "Expect '(' after function name.");
    if (!check(TokenType::RightParenthesis)) {
        do {
            g_current_compiler->function->arity()++;
            if (g_current_compiler->function->arity() > MAX_ARITY) {
                error_at_current("Can't have more than 255 parameters.");
            }
            Byte constant = parse_variable("Expect parameter name.");
            define_variable(constant);
        } while (match(TokenType::Comma));
    }
    consume(TokenType::RightParenthesis, "Expect ')' after parameters.");
    consume(TokenType::LeftBrace, "Expect '{' before function body.");
    block();

    auto * function = end_compiler();
    emit_bytes(OpCode::Closure, make_constant(Value::obj(function)));

    for (const auto & upvalue : compiler.upvalues) {
        emit_bytes(upvalue.is_local ? Byte(1) : Byte(0), upvalue.index);
    }
}

auto method() -> void
{
    consume(TokenType::Identifier, "Expect method name.");
    Byte constant = identifier_constant(g_parser.previous);

    auto type
            = g_parser.previous.lexeme == "init" ? FunctionType::Initializer : FunctionType::Method;
    function(type);

    emit_bytes(OpCode::Method, constant);
}

auto class_declaration() -> void
{
    consume(TokenType::Identifier, "Expect class name.");
    Token class_name = g_parser.previous;
    Byte name_constant = identifier_constant(g_parser.previous);
    declare_variable();

    emit_bytes(OpCode::Class, name_constant);
    define_variable(name_constant);

    ClassCompiler class_compiler{.enclosing = g_current_class, .has_superclass = false};
    g_current_class = &class_compiler;

    if (match(TokenType::Less)) {
        consume(TokenType::Identifier, "Expect superclass name.");
        variable({.can_assign = false});

        if (class_name.lexeme == g_parser.previous.lexeme) {
            error("A class can't inherit from itself.");
        }

        begin_scope();
        add_local(synthetic_token("super"));
        define_variable(0);

        named_variable(class_name, {.can_assign = false});
        emit_byte(OpCode::Inherit);
        class_compiler.has_superclass = true;
    }

    named_variable(class_name, {.can_assign = false});

    consume(TokenType::LeftBrace, "Expect '{' before class body.");
    while (!check(TokenType::RightBrace) && !check(TokenType::EndOfFile)) {
        method();
    }
    consume(TokenType::RightBrace, "Expect '}' after class body.");
    emit_byte(OpCode::Pop);

    if (g_current_class->has_superclass) {
        end_scope();
    }

    g_current_class = g_current_class->enclosing;
}

auto fun_declaration() -> void
{
    Byte global = parse_variable("Expect function name.");
    mark_initialized();

    function(FunctionType::Function);

    define_variable(global);
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
    SourceLocation prev_op_sloc = g_parser.op_sloc;
    g_parser.op_sloc = g_parser.current.sloc;

    if (match(TokenType::Print)) {
        print_statement();
    }
    else if (match(TokenType::If)) {
        if_statement();
    }
    else if (match(TokenType::Return)) {
        return_statement();
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

    g_parser.op_sloc = prev_op_sloc;
}

auto declaration() -> void
{
    SourceLocation prev_op_sloc = g_parser.op_sloc;
    g_parser.op_sloc = g_parser.current.sloc;

    if (match(TokenType::Class)) {
        class_declaration();
    }
    else if (match(TokenType::Fun)) {
        fun_declaration();
    }
    else if (match(TokenType::Var)) {
        var_declaration();
    }
    else {
        statement();
    }

    g_parser.op_sloc = prev_op_sloc;

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
    rules[to_idx(TokenType::Dot)]             = {.prefix = nullptr,  .infix = dot,     .precedence = Call};
    rules[to_idx(TokenType::EqualEqual)]      = {.prefix = nullptr,  .infix = binary,  .precedence = Equality};
    rules[to_idx(TokenType::False)]           = {.prefix = literal,  .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::Greater)]         = {.prefix = nullptr,  .infix = binary,  .precedence = Comparison};
    rules[to_idx(TokenType::GreaterEqual)]    = {.prefix = nullptr,  .infix = binary,  .precedence = Comparison};
    rules[to_idx(TokenType::Identifier)]      = {.prefix = variable, .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::LeftParenthesis)] = {.prefix = grouping, .infix = call,    .precedence = Call};
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
    rules[to_idx(TokenType::Super)]           = {.prefix = super_ex, .infix = nullptr, .precedence = None};
    rules[to_idx(TokenType::This)]            = {.prefix = this_ex,  .infix = nullptr, .precedence = None};
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
    SourceLocation prev_op_sloc = g_parser.op_sloc;
    g_parser.op_sloc = g_parser.current.sloc;

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

    g_parser.op_sloc = prev_op_sloc;

    if (ctx.can_assign && match(TokenType::Equal)) {
        error("Invalid assignment target.");
    }
}

} // namespace

// TODO: should denote failure, replace with std::expected
export auto compile(std::string_view source) -> ObjFunction *
{
    Compiler compiler;
    init_compiler(compiler, FunctionType::Script);

    init_scanner(source);
    advance();

    while (!match(TokenType::EndOfFile)) {
        declaration();
    }

    auto * function = end_compiler();
    return g_parser.had_error ? nullptr : function;
}

} // namespace cpplox