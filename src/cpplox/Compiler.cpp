module;

#include <boost/preprocessor.hpp>

#include <cassert>

module cpplox;

import std;

import :Chunk;
import :Code;
import :Compiler;
import :OpCode;
import :Scanner;

import magic_enum;

namespace cpplox {

namespace {
constexpr const std::size_t MAX_ARITY = 255;

class Parser
{
public:
    explicit Parser(ScannerPtr && scanner)
        : m_scanner(std::move(scanner))
    {
    }

    [[nodiscard]] auto get_previous() const -> const Token & { return m_previous; }
    [[nodiscard]] auto get_current() const -> const Token & { return m_current; }
    [[nodiscard]] auto had_errors() const -> bool { return m_had_error; }
    [[nodiscard]] auto is_panic_mode() const -> bool { return m_panic_mode; }

    auto error_at(const Token & token, std::string_view message) -> void
    {
        if (m_panic_mode) {
            return;
        }
        m_panic_mode = true;

        std::print(std::cerr, "[{}:{}] Error", token.sloc.line, token.sloc.column);

        if (token.type == TokenType::EndOfFile) {
            std::print(std::cerr, " at end");
        }
        else if (token.type != TokenType::Error) {
            std::print(std::cerr, " at '{}'", token.lexeme);
        }

        std::println(std::cerr, ": {}", message);

        m_had_error = true;
    }

    auto error(std::string_view message) -> void { error_at(m_previous, message); }
    auto error_at_current(std::string_view message) -> void { error_at(m_current, message); }

    auto advance() -> void
    {
        m_previous = m_current;

        for (;;) {
            m_current = m_scanner->next_token();
            if (m_current.type != TokenType::Error) {
                break;
            }
            error_at_current(m_current.lexeme);
        }
    }

    auto consume(TokenType type, std::string_view message) -> void
    {
        if (m_current.type == type) {
            advance();
            return;
        }

        error_at_current(message);
    }

    [[nodiscard]] auto check(TokenType type) const -> bool { return m_current.type == type; }

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

        m_panic_mode = false;
        while (m_current.type != EndOfFile) {
            if (m_previous.type == Semicolon) {
                return;
            }
            switch (m_current.type) {
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

private:
    ScannerPtr m_scanner;

    Token m_current;
    Token m_previous;
    bool m_had_error = false;
    // TODO: prevents cascading errors, see error_at. Is there a better way to achieve this?
    bool m_panic_mode = false;
};

} // namespace

class Compiler
{
private:
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

    using ParseFn = void (Compiler::*)(ParseContext);

    struct ParseRule
    {
        ParseFn prefix = nullptr;
        ParseFn infix = nullptr;
        Precedence precedence = Precedence::None;
    };

    struct Local
    {
        Token name;
        int depth;
        bool is_captured;
        bool is_const;
    };

    struct Upvalue
    {
        Byte index;
        bool is_local;
        bool is_const;
    };

    enum class FunctionType : std::uint8_t
    {
        Anonymous,
        Function,
        Initializer,
        Method,
        Script,
    };

    struct LoopCompiler
    {
        std::size_t loop_start;
        std::vector<std::size_t> loop_breaks;
    };

    struct FunctionCompiler
    {
        Code code;
        FunctionType type = FunctionType::Script;

        std::vector<Local> locals;
        std::vector<Upvalue> upvalues;
        std::vector<LoopCompiler> loops;
        int scope_depth = 0;
    };

    struct ClassCompiler
    {
        std::string_view name;
        bool has_superclass = false;
    };

public:
    explicit Compiler(Parser && parser)
        : m_parser(std::move(parser))
    {
        init_function(FunctionType::Script);
    }

    [[nodiscard]] auto compile() -> std::optional<std::vector<Code>>
    {
        m_parser.advance();

        while (!m_parser.match(TokenType::EndOfFile)) {
            declaration();
        }

        FunctionCompiler script_compiler = end_function();
        m_code.push_back(std::move(script_compiler.code));
        return m_parser.had_errors() ? std::nullopt : std::optional{std::move(m_code)};
    }

private:
    [[nodiscard]] auto current_loop() -> LoopCompiler & { return current_function().loops.back(); }
    [[nodiscard]] auto current_function() -> FunctionCompiler &
    {
        return m_function_compilers.back();
    }
    [[nodiscard]] auto current_class() -> ClassCompiler & { return m_class_compilers.back(); }

    [[nodiscard]] auto current_code() -> Code & { return current_function().code; }

    // *** Byte Code Emitter ***

    auto emit_byte(Byte byte) -> void { current_code().write(byte, m_op_sloc); }
    auto emit_byte(OpCode op) -> void { current_code().write(op, m_op_sloc); }

    template <typename ByteT, typename... Bytes>
    auto emit_bytes(ByteT byte, Bytes... bytes) -> void
    {
        emit_byte(byte);
        if constexpr (sizeof...(bytes) > 0) {
            emit_bytes(bytes...);
        }
    }

    auto emit_loop(std::size_t start) -> void
    {
        emit_byte(OpCode::Loop);

        std::size_t offset = current_code().code().size() - start + 2;
        if (offset > DOUBLE_BYTE_MAX) {
            m_parser.error("Loop body too large.");
        }

        emit_byte(static_cast<Byte>((offset >> BYTE_DIGITS) & BYTE_MAX));
        emit_byte(static_cast<Byte>(offset & BYTE_MAX));
    }

    auto emit_jump(OpCode instruction) -> std::size_t
    {
        emit_bytes(instruction, BYTE_MAX, BYTE_MAX);

        return current_code().code().size() - 2;
    }

    auto patch_jump(std::size_t offset) -> void
    {
        std::size_t jump_length = current_code().code().size() - offset - 2;

        if (jump_length > DOUBLE_BYTE_MAX) {
            m_parser.error("Too much code to jump over.");
        }

        current_code().code()[offset] = (jump_length >> BYTE_DIGITS) & BYTE_MAX;
        current_code().code()[offset + 1] = jump_length & BYTE_MAX;
    }

    auto make_constant(CompiledValue value) -> Byte
    {
        std::size_t c = current_code().add_constant(std::move(value));
        if (c >= BYTE_MAX) {
            m_parser.error("Too many constants in one chunk.");
            return 0;
        }

        return static_cast<Byte>(c);
    }

    auto emit_constant(CompiledValue value) -> void
    {
        emit_bytes(OpCode::Constant, make_constant(std::move(value)));
    }

    auto emit_return() -> void
    {
        if (current_function().type == FunctionType::Initializer) {
            emit_bytes(OpCode::GetLocal, static_cast<Byte>(0));
        }
        else {
            emit_byte(OpCode::Nil);
        }
        emit_byte(OpCode::Return);
    }

    // ***

    auto make_function(FunctionType type) -> FunctionCompiler
    {
        auto get_name = [type, this] -> std::string {
            if (type == FunctionType::Anonymous) {
                return "[anonymous]";
            }
            if (type == FunctionType::Function) {
                return std::string{m_parser.get_previous().lexeme};
            }
            if (type == FunctionType::Method || type == FunctionType::Initializer) {
                return std::format("{}.{}", current_class().name, m_parser.get_previous().lexeme);
            }
            return "";
        };

        return {
            .code = Code(get_name(), m_parser.get_previous().sloc),
            .type = type,
            .locals = {
                {
                    .name = {
                        .type = TokenType::EndOfFile,
                        .lexeme = type == FunctionType::Function ? "" : "this",
                        .sloc = {},
                    },
                    .depth = 0,
                    .is_captured = false,
                    .is_const = true,
                },
            },
            .upvalues = {},
            .loops = {},
            .scope_depth = 0,
        };
    }

    auto init_function(FunctionType type) -> void
    {
        m_function_compilers.push_back(make_function(type));
    }
    auto end_function() -> FunctionCompiler
    {
        emit_return();
        FunctionCompiler compiler = std::move(m_function_compilers.back());
        m_function_compilers.pop_back();
        return compiler;
    }

    auto is_scope_local() -> bool { return current_function().scope_depth > 0; }

    auto begin_scope() -> void { current_function().scope_depth++; }

    auto end_scope() -> void
    {
        auto & func = current_function();
        func.scope_depth--;

        while (func.locals.size() > 0 && func.locals.back().depth > func.scope_depth) {
            emit_byte(func.locals.back().is_captured ? OpCode::CloseUpvalue : OpCode::Pop);
            func.locals.pop_back();
        }
    }

    auto identifier_constant(const Token & name) -> Byte
    {
        return make_constant(std::string{name.lexeme});
    }

    auto synthetic_token(std::string_view name) -> Token
    {
        return {
                .type = TokenType::Identifier,
                .lexeme = name,
                .sloc = m_parser.get_previous().sloc,
        };
    }

    auto add_local(const Token & name, bool is_const) -> void
    {
        if (current_function().locals.size() > BYTE_MAX) {
            m_parser.error("Too many local variables in function.");
            return;
        }

        current_function().locals.push_back({
                .name = name,
                .depth = -1,
                .is_captured = false,
                .is_const = is_const,
        });
    }

    auto resolve_local(FunctionCompiler & compiler, const Token & name)
            -> std::optional<std::pair<std::size_t, Local &>>
    {
        // FIXME: UHH why `vector | views::reverse` doesn't work??? libstdc++ wtf???
        // TODO: use views::enumerate once it's available in libc++
        for (const auto & [idx, local] :
             std::views::zip(std::views::iota(0UZ), compiler.locals) | std::views::reverse) {
            if (local.name.lexeme == name.lexeme) {
                if (local.depth == -1) {
                    m_parser.error("Cannot read local variable in its own initializer.");
                }
                return {{idx, local}};
            }
        }
        return std::nullopt;
    }

    auto resolve_local(const Token & name) -> std::optional<std::pair<std::size_t, Local &>>
    {
        return resolve_local(current_function(), name);
    }

    auto add_upvalue(FunctionCompiler & compiler, Byte index, bool is_local, bool is_const)
            -> std::pair<std::size_t, Upvalue &>
    {
        // TODO: use views::enumerate once it's available in libc++
        for (const auto & [idx, upvalue] :
             std::views::zip(std::views::iota(0UZ), compiler.upvalues)) {
            if (upvalue.index == index && upvalue.is_local == is_local) {
                return {idx, upvalue};
            }
        }

        if (compiler.upvalues.size() >= BYTE_MAX) {
            m_parser.error("Too many closure variables in function.");
            return {0, compiler.upvalues[0]};
        }

        compiler.upvalues.push_back({.index = index, .is_local = is_local, .is_const = is_const});
        return {compiler.code.upvalue_count()++, compiler.upvalues.back()};
    }

    // NOLINTNEXTLINE(misc-no-recursion)
    auto resolve_upvalue(std::size_t comp_idx, const Token & name)
            -> std::optional<std::pair<std::size_t, Upvalue &>>
    {
        if (comp_idx == 0) {
            return std::nullopt;
        }

        auto & current = m_function_compilers[comp_idx];
        auto & enclosing = m_function_compilers[comp_idx - 1];

        auto maybe_local = resolve_local(enclosing, name);
        if (maybe_local.has_value()) {
            auto & [idx, local] = maybe_local.value();
            local.is_captured = true;
            return add_upvalue(
                    current, static_cast<Byte>(idx), /* is_local = */ true, local.is_const
            );
        }

        auto maybe_upvalue = resolve_upvalue(comp_idx - 1, name);
        if (maybe_upvalue.has_value()) {
            auto & [idx, upvalue] = maybe_upvalue.value();
            return add_upvalue(
                    current, static_cast<Byte>(idx), /* is_local = */ false, upvalue.is_const
            );
        }

        return std::nullopt;
    }

    auto resolve_upvalue(const Token & name) -> std::optional<std::pair<std::size_t, Upvalue &>>
    {
        return resolve_upvalue(m_function_compilers.size() - 1, name);
    }

    auto mark_initialized() -> void
    {
        if (current_function().scope_depth == 0) {
            return;
        }
        current_function().locals.back().depth = current_function().scope_depth;
    }

    auto declare_variable(bool is_const) -> void
    {
        if (!is_scope_local()) {
            return;
        }

        const Token & name = m_parser.get_previous();
        for (const auto & local : std::ranges::reverse_view{current_function().locals}) {
            if (local.depth != -1 && local.depth < current_function().scope_depth) {
                break;
            }

            if (local.name.lexeme == name.lexeme) {
                m_parser.error("Already a variable with this name in this scope.");
            }
        }

        add_local(m_parser.get_previous(), is_const);
    }

    auto parse_variable(bool is_const, std::string_view error_message) -> Byte
    {
        m_parser.consume(TokenType::Identifier, error_message);
        declare_variable(is_const);

        if (is_scope_local()) {
            return 0;
        }

        return identifier_constant(m_parser.get_previous());
    }

    auto define_variable(Byte global, bool is_const) -> void
    {
        if (is_scope_local()) {
            mark_initialized();
            return;
        }
        emit_bytes(is_const ? OpCode::DefineGlobalConst : OpCode::DefineGlobalVar, global);
    }

    // *** Expression Parser ***

    auto parse_precedence(Precedence precedence) -> void
    {
        SourceLocation prev_op_sloc = m_op_sloc;
        m_op_sloc = m_parser.get_current().sloc;

        m_parser.advance();
        auto prefix_rule = get_rule(m_parser.get_previous().type).prefix;
        if (prefix_rule == nullptr) {
            m_parser.error("Expect expression.");
            return;
        }

        ParseContext ctx{.can_assign = precedence <= Precedence::Assignment};
        (this->*prefix_rule)(ctx);

        while (precedence <= get_rule(m_parser.get_current().type).precedence) {
            m_parser.advance();
            auto infix_rule = get_rule(m_parser.get_previous().type).infix;
            assert(infix_rule != nullptr);
            (this->*infix_rule)(ctx);
        }

        m_op_sloc = prev_op_sloc;

        if (ctx.can_assign && m_parser.match(TokenType::Equal)) {
            m_parser.error("Invalid assignment target.");
        }
    }

    auto next_precedence(Precedence precedence) -> Precedence
    {
        return static_cast<Precedence>(static_cast<std::uint8_t>(precedence) + 1);
    }

    auto expression() -> void { parse_precedence(Precedence::Assignment); }

    auto number(ParseContext /* ctx */) -> void
    {
        double value = 0;

        [[maybe_unused]] auto result = std::from_chars(
                m_parser.get_previous().lexeme.begin(), m_parser.get_previous().lexeme.end(), value
        );
        assert((result.ec == std::errc{}) && "Cannot parse Number token provided by Scanner");

        emit_constant(value);
    }

    auto grouping(ParseContext /* ctx */) -> void
    {
        expression();
        m_parser.consume(TokenType::RightParenthesis, "Expect ')' after expression.");
    }

    auto unary(ParseContext /* ctx */) -> void
    {
        TokenType operator_type = m_parser.get_previous().type;

        parse_precedence(Precedence::Unary);

        switch (operator_type) {
        case TokenType::Bang: emit_byte(OpCode::Not); break;
        case TokenType::Minus: emit_byte(OpCode::Negate); break;
        default: m_parser.error("Unknown unary operand.");
        }
    }

    auto binary(ParseContext /* ctx */) -> void
    {
        TokenType operator_type = m_parser.get_previous().type;
        parse_precedence(next_precedence(get_rule(operator_type).precedence));

        switch (operator_type) {
        case TokenType::BangEqual: emit_bytes(OpCode::Equal, OpCode::Not); break;
        case TokenType::EqualEqual: emit_byte(OpCode::Equal); break;

        case TokenType::Greater: emit_byte(OpCode::Greater); break;
        case TokenType::GreaterEqual: emit_bytes(OpCode::Less, OpCode::Not); break;
        case TokenType::Less: emit_byte(OpCode::Less); break;
        case TokenType::LessEqual: emit_bytes(OpCode::Greater, OpCode::Not); break;

        case TokenType::Plus: emit_byte(OpCode::Add); break;
        case TokenType::Minus: emit_byte(OpCode::Subtract); break;
        case TokenType::Star: emit_byte(OpCode::Multiply); break;
        case TokenType::Slash: emit_byte(OpCode::Divide); break;
        default: m_parser.error("Unknown binary operand.");
        }
    }

    auto literal(ParseContext /* ctx */) -> void
    {
        switch (m_parser.get_previous().type) {
        case TokenType::False: emit_byte(OpCode::False); break;
        case TokenType::True: emit_byte(OpCode::True); break;
        case TokenType::Nil: emit_byte(OpCode::Nil); break;
        default: m_parser.error("Unknown literal.");
        }
    }

    auto fun_ex(ParseContext /* ctx */) -> void { function(FunctionType::Anonymous); }

    auto string(ParseContext /* ctx */) -> void
    {
        auto lexeme = m_parser.get_previous().lexeme;
        emit_constant(std::string{lexeme.substr(1, lexeme.length() - 2)});
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
        bool is_const = false;

        if (auto local_pos = resolve_local(name); local_pos.has_value()) {
            auto & [pos, local] = local_pos.value();
            get_op = OpCode::GetLocal;
            set_op = OpCode::SetLocal;
            arg = static_cast<Byte>(pos);
            is_const = local.is_const;
        }
        else if (auto upvalue_pos = resolve_upvalue(name); upvalue_pos.has_value()) {
            auto & [pos, upvalue] = upvalue_pos.value();
            get_op = OpCode::GetUpvalue;
            set_op = OpCode::SetUpvalue;
            arg = static_cast<Byte>(pos);
            is_const = upvalue.is_const;
        }
        else {
            get_op = OpCode::GetGlobal;
            set_op = OpCode::SetGlobal;
            arg = identifier_constant(name);
            // Global const assignment is checked at runtime
            is_const = false;
        }

        if (ctx.can_assign && m_parser.match(TokenType::Equal)) {
            if (is_const) {
                m_parser.error("Cannot assign to const variable.");
            }
            expression();
            emit_bytes(set_op, arg);
        }
        else {
            emit_bytes(get_op, arg);
        }
    }

    auto variable(ParseContext ctx) -> void { named_variable(m_parser.get_previous(), ctx); }

    auto this_ex(ParseContext /* ctx */) -> void
    {
        if (m_class_compilers.empty()) {
            m_parser.error("Cannot use 'this' outside of a class.");
        }

        variable({.can_assign = false});
    }

    auto argument_list() -> Byte
    {
        std::size_t arg_count = 0;
        if (!m_parser.check(TokenType::RightParenthesis)) {
            do {
                expression();
                arg_count++;
                if (arg_count > BYTE_MAX) {
                    m_parser.error("Cannot have more than 255 arguments.");
                }
            } while (m_parser.match(TokenType::Comma));
        }
        m_parser.consume(TokenType::RightParenthesis, "Expect ')' after arguments.");
        return static_cast<Byte>(arg_count);
    }

    auto call(ParseContext /* ctx */) -> void
    {
        Byte arg_count = argument_list();
        emit_bytes(OpCode::Call, arg_count);
    }

    auto super_ex(ParseContext /* ctx */) -> void
    {
        if (m_class_compilers.empty()) {
            m_parser.error("Cannot use 'super' outside of a class.");
        }
        else if (!current_class().has_superclass) {
            m_parser.error("Cannot use 'super' in a class with no superclass.");
        }

        m_parser.consume(TokenType::Dot, "Expect '.' after 'super'.");

        SourceLocation prev_op_sloc = m_op_sloc;
        m_op_sloc = m_parser.get_current().sloc;

        m_parser.consume(TokenType::Identifier, "Expect superclass method name.");

        Byte name = identifier_constant(m_parser.get_previous());

        named_variable(synthetic_token("this"), {.can_assign = false});

        if (m_parser.match(TokenType::LeftParenthesis)) {
            Byte arg_count = argument_list();
            named_variable(synthetic_token("super"), {.can_assign = false});
            emit_bytes(OpCode::SuperInvoke, name);
            emit_byte(arg_count);
        }
        else {
            named_variable(synthetic_token("super"), {.can_assign = false});
            emit_bytes(OpCode::GetSuper, name);
        }

        m_op_sloc = prev_op_sloc;
    }

    auto dot(ParseContext ctx) -> void
    {
        SourceLocation prev_op_sloc = m_op_sloc;
        m_op_sloc = m_parser.get_current().sloc;

        m_parser.consume(TokenType::Identifier, "Expect property name after '.'.");
        Byte name = identifier_constant(m_parser.get_previous());

        if (ctx.can_assign && m_parser.match(TokenType::Equal)) {
            expression();
            emit_bytes(OpCode::SetProperty, name);
        }
        else if (m_parser.match(TokenType::LeftParenthesis)) {
            Byte arg_count = argument_list();
            emit_bytes(OpCode::Invoke, name);
            emit_byte(arg_count);
        }
        else {
            emit_bytes(OpCode::GetProperty, name);
        }

        m_op_sloc = prev_op_sloc;
    }

    // *** Statement Parser ***

    // It's a recursive descent parser, duh!
    // NOLINTBEGIN(misc-no-recursion)
    auto var_declaration(bool is_const) -> void
    {
        Byte global = parse_variable(is_const, "Expect variable name.");

        bool has_initializer = m_parser.check(TokenType::Equal);
        if (has_initializer) {
            m_parser.advance();
            expression();
        }
        else {
            if (is_const) {
                m_parser.error("Const variable must have an initializer.");
            }
            emit_byte(OpCode::Nil);
        }
        m_parser.consume(TokenType::Semicolon, "Expect ';' after variable declaration.");

        define_variable(global, is_const);
    }

    auto print_statement() -> void
    {
        expression();
        m_parser.consume(TokenType::Semicolon, "Expect ';' after value.");
        emit_byte(OpCode::Print);
    }

    auto return_statement() -> void
    {
        if (current_function().type == FunctionType::Script) {
            m_parser.error("Cannot return from top-level code.");
        }

        if (m_parser.match(TokenType::Semicolon)) {
            emit_return();
        }
        else {
            if (current_function().type == FunctionType::Initializer) {
                m_parser.error("Cannot return a value from initializer.");
            }

            expression();
            m_parser.consume(TokenType::Semicolon, "Expect ';' after return value.");
            emit_byte(OpCode::Return);
        }
    }

    auto expression_statement() -> void
    {
        expression();
        m_parser.consume(TokenType::Semicolon, "Expect ';' after expression.");
        emit_byte(OpCode::Pop);
    }

    auto block() -> void
    {
        while (!m_parser.check(TokenType::RightBrace) && !m_parser.check(TokenType::EndOfFile)) {
            declaration();
        }

        m_parser.consume(TokenType::RightBrace, "Expect '}' after block.");
    }

    auto function(FunctionType type) -> void
    {
        init_function(type);

        begin_scope();

        m_parser.consume(
                TokenType::LeftParenthesis,
                type == FunctionType::Anonymous ? "Expect '(' after 'fun'."
                                                : "Expect '(' after function name."
        );
        if (!m_parser.check(TokenType::RightParenthesis)) {
            do {
                current_function().code.arity()++;
                if (current_function().code.arity() > MAX_ARITY) {
                    m_parser.error_at_current("Cannot have more than 255 parameters.");
                }
                bool is_const = m_parser.match(TokenType::Const);
                Byte constant = parse_variable(is_const, "Expect parameter name.");
                define_variable(constant, is_const);
            } while (m_parser.match(TokenType::Comma));
        }
        m_parser.consume(TokenType::RightParenthesis, "Expect ')' after parameters.");
        m_parser.consume(TokenType::LeftBrace, "Expect '{' before function body.");
        block();

        FunctionCompiler compiler = end_function();
        emit_bytes(
                OpCode::Closure,
                make_constant(
                        FunctionReference{
                                .name = std::string{compiler.code.get_name()},
                                .sloc = compiler.code.get_location(),
                        }
                )
        );

        for (const auto & upvalue : compiler.upvalues) {
            emit_bytes(upvalue.is_local ? Byte(1) : Byte(0), upvalue.index);
        }
        m_code.push_back(std::move(compiler.code));
    }

    auto method() -> void
    {
        m_parser.consume(TokenType::Identifier, "Expect method name.");
        Byte constant = identifier_constant(m_parser.get_previous());

        auto type = m_parser.get_previous().lexeme == "init" ? FunctionType::Initializer
                                                             : FunctionType::Method;
        function(type);

        emit_bytes(OpCode::Method, constant);
    }

    auto class_declaration() -> void
    {
        m_parser.consume(TokenType::Identifier, "Expect class name.");
        Token class_name = m_parser.get_previous();
        Byte name_constant = identifier_constant(m_parser.get_previous());
        declare_variable(/* is_const = */ true);

        emit_bytes(OpCode::Class, name_constant);
        define_variable(name_constant, /* is_const = */ true);

        m_class_compilers.push_back({
                .name = class_name.lexeme,
                .has_superclass = false,
        });

        if (m_parser.match(TokenType::Less)) {
            m_parser.consume(TokenType::Identifier, "Expect superclass name.");
            variable({.can_assign = false});

            if (class_name.lexeme == m_parser.get_previous().lexeme) {
                m_parser.error("A class cannot inherit from itself.");
            }

            begin_scope();
            add_local(synthetic_token("super"), /* is_const = */ true);
            define_variable(0, /* is_const = */ true);

            named_variable(class_name, {.can_assign = false});
            emit_byte(OpCode::Inherit);
            current_class().has_superclass = true;
        }

        named_variable(class_name, {.can_assign = false});

        m_parser.consume(TokenType::LeftBrace, "Expect '{' before class body.");
        while (!m_parser.check(TokenType::RightBrace) && !m_parser.check(TokenType::EndOfFile)) {
            method();
        }
        m_parser.consume(TokenType::RightBrace, "Expect '}' after class body.");
        emit_byte(OpCode::Pop);

        if (current_class().has_superclass) {
            end_scope();
        }
    }

    auto fun_declaration() -> void
    {
        Byte global = parse_variable(/* is_const = */ true, "Expect function name.");
        mark_initialized();

        function(FunctionType::Function);

        define_variable(global, /* is_const = */ true);
    }

    auto if_statement() -> void
    {
        m_parser.consume(TokenType::LeftParenthesis, "Expect '(' after 'if'.");
        expression();
        m_parser.consume(TokenType::RightParenthesis, "Expect ')' after condition.");

        std::size_t then_jump = emit_jump(OpCode::JumpIfFalseAndPop);

        statement();

        std::size_t else_jump = emit_jump(OpCode::Jump);

        patch_jump(then_jump);

        if (m_parser.match(TokenType::Else)) {
            statement();
        }

        patch_jump(else_jump);
    }

    auto while_statement() -> void
    {
        current_function().loops.push_back(
                {.loop_start = current_code().code().size(), .loop_breaks = {}}
        );

        m_parser.consume(TokenType::LeftParenthesis, "Expect '(' after 'while'.");
        expression();
        m_parser.consume(TokenType::RightParenthesis, "Expect ')' after condition.");

        current_loop().loop_breaks.push_back(emit_jump(OpCode::JumpIfFalseAndPop));

        statement();
        emit_loop(current_loop().loop_start);

        for (const auto loop_break : current_loop().loop_breaks) {
            patch_jump(loop_break);
        }

        current_function().loops.pop_back();
    }

    auto for_statement() -> void
    {
        // Completely atrocious -- so many weird jumps! TODO - should revisit AST creation and
        // double-pass approach.

        begin_scope();

        m_parser.consume(TokenType::LeftParenthesis, "Expect '(' after 'for'.");
        if (m_parser.match(TokenType::Semicolon)) {
            // No initializer
        }
        else if (m_parser.match(TokenType::Var)) {
            var_declaration(/* is_const = */ false);
        }
        else if (m_parser.match(TokenType::Const)) {
            var_declaration(/* is_const = */ true);
        }
        else {
            expression_statement();
        }

        current_function().loops.push_back(
                {.loop_start = current_code().code().size(), .loop_breaks = {}}
        );

        if (!m_parser.match(TokenType::Semicolon)) {
            expression();
            m_parser.consume(TokenType::Semicolon, "Expect ';' after loop condition.");

            current_loop().loop_breaks.push_back(emit_jump(OpCode::JumpIfFalseAndPop));
        }

        if (!m_parser.match(TokenType::RightParenthesis)) {
            std::size_t body_jump = emit_jump(OpCode::Jump);
            std::size_t increment_start = current_code().code().size();

            expression();

            emit_byte(OpCode::Pop);
            m_parser.consume(TokenType::RightParenthesis, "Expect ')' after for clauses.");

            emit_loop(current_loop().loop_start);
            current_loop().loop_start = increment_start;
            patch_jump(body_jump);
        }

        statement();
        emit_loop(current_loop().loop_start);

        for (const auto loop_break : current_loop().loop_breaks) {
            patch_jump(loop_break);
        }

        current_function().loops.pop_back();
        end_scope();
    }

    auto break_statement() -> void
    {
        if (current_function().loops.empty()) {
            m_parser.error("Cannot use 'break' outside of a loop.");
            return;
        }
        m_parser.consume(TokenType::Semicolon, "Expect ';' after 'break'.");
        current_loop().loop_breaks.push_back(emit_jump(OpCode::Jump));
    }

    auto continue_statement() -> void
    {
        if (current_function().loops.empty()) {
            m_parser.error("Cannot use 'continue' outside of a loop.");
            return;
        }
        m_parser.consume(TokenType::Semicolon, "Expect ';' after 'continue'.");
        emit_loop(current_loop().loop_start);
    }

    auto statement() -> void
    {
        SourceLocation prev_op_sloc = m_op_sloc;
        m_op_sloc = m_parser.get_current().sloc;

        if (m_parser.match(TokenType::Print)) {
            print_statement();
        }
        else if (m_parser.match(TokenType::If)) {
            if_statement();
        }
        else if (m_parser.match(TokenType::Return)) {
            return_statement();
        }
        else if (m_parser.match(TokenType::While)) {
            while_statement();
        }
        else if (m_parser.match(TokenType::For)) {
            for_statement();
        }
        else if (m_parser.match(TokenType::Break)) {
            break_statement();
        }
        else if (m_parser.match(TokenType::Continue)) {
            continue_statement();
        }
        else if (m_parser.match(TokenType::LeftBrace)) {
            begin_scope();
            block();
            end_scope();
        }
        else {
            expression_statement();
        }

        m_op_sloc = prev_op_sloc;
    }

    auto declaration() -> void
    {
        SourceLocation prev_op_sloc = m_op_sloc;
        m_op_sloc = m_parser.get_current().sloc;

        if (m_parser.match(TokenType::Class)) {
            class_declaration();
        }
        else if (m_parser.match(TokenType::Fun)) {
            fun_declaration();
        }
        else if (m_parser.match(TokenType::Var)) {
            var_declaration(/* is_const = */ false);
        }
        else if (m_parser.match(TokenType::Const)) {
            var_declaration(/* is_const = */ true);
        }
        else {
            statement();
        }

        m_op_sloc = prev_op_sloc;

        if (m_parser.is_panic_mode()) {
            m_parser.synchronize();
        }
    }
    // NOLINTEND(misc-no-recursion)

    // oh my god holy shit
    consteval static auto generate_rule_table() noexcept
    {
        using enum Precedence;
        std::array<ParseRule, magic_enum::enum_values<TokenType>().size()> rules;

        // clang-format off

        // NOLINTBEGIN(cppcoreguidelines-macro-usage)
        #define _IS_NULLPTR_CHECK_nullptr // Empty macro to assist with CHECK_EMPTY below
        #define IS_NULLPTR(val) BOOST_PP_CHECK_EMPTY(_IS_NULLPTR_CHECK_ ## val)

        #define ADD_RULE(token_type, prefix_val, infix_val, precendence_val)                       \
            rules[static_cast<std::size_t>(TokenType::token_type)] = {                             \
                    .prefix = BOOST_PP_IIF(IS_NULLPTR(prefix_val), nullptr, &Compiler::prefix_val),\
                    .infix = BOOST_PP_IIF(IS_NULLPTR(infix_val), nullptr, &Compiler::infix_val),   \
                    .precedence = Precedence::precendence_val,                                     \
            }

        //       Token type     | Prefix fn | Infix fn | Precedence
        ADD_RULE(And,             nullptr,    and_ex,    And);
        ADD_RULE(Bang,            unary,      nullptr,   None);
        ADD_RULE(BangEqual,       nullptr,    binary,    Equality);
        ADD_RULE(Dot,             nullptr,    dot,       Call);
        ADD_RULE(EqualEqual,      nullptr,    binary,    Equality);
        ADD_RULE(False,           literal,    nullptr,   None);
        ADD_RULE(Fun,             fun_ex,     nullptr,   None);
        ADD_RULE(Greater,         nullptr,    binary,    Comparison);
        ADD_RULE(GreaterEqual,    nullptr,    binary,    Comparison);
        ADD_RULE(Identifier,      variable,   nullptr,   None);
        ADD_RULE(LeftParenthesis, grouping,   call,      Call);
        ADD_RULE(Less,            nullptr,    binary,    Comparison);
        ADD_RULE(LessEqual,       nullptr,    binary,    Comparison);
        ADD_RULE(Minus,           unary,      binary,    Term);
        ADD_RULE(Nil,             literal,    nullptr,   None);
        ADD_RULE(Number,          number,     nullptr,   None);
        ADD_RULE(Or,              nullptr,    or_ex,     Or);
        ADD_RULE(Plus,            nullptr,    binary,    Term);
        ADD_RULE(Slash,           nullptr,    binary,    Factor);
        ADD_RULE(Star,            nullptr,    binary,    Factor);
        ADD_RULE(String,          string,     nullptr,   None);
        ADD_RULE(Super,           super_ex,   nullptr,   None);
        ADD_RULE(This,            this_ex,    nullptr,   None);
        ADD_RULE(True,            literal,    nullptr,   None);

        #undef _IS_NULLPTR_CHECK_nullptr
        #undef IS_NULLPTR
        #undef ADD_RULE
        // NOLINTEND(cppcoreguidelines-macro-usage)

        // clang-format on

        return rules;
    }

    static auto get_rule(TokenType type) -> const ParseRule &
    {
        constexpr static const auto s_rule_table = generate_rule_table();
        // Accessing a table via TokenType is safe, it has elements precisely for each TokenType
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
        return s_rule_table[static_cast<std::size_t>(type)];
    }

private:
    std::vector<FunctionCompiler> m_function_compilers;
    std::vector<ClassCompiler> m_class_compilers;
    std::vector<Code> m_code;
    Parser m_parser;
    SourceLocation m_op_sloc = {.line = 1, .column = 1};
};

namespace {

} // namespace

// TODO: should denote failure, replace with std::expected
auto compile(std::string_view source) -> std::optional<std::vector<Code>>
{
    ScannerPtr scanner = make_scanner(source);
    Parser parser(std::move(scanner));
    Compiler compiler(std::move(parser));
    return compiler.compile();
}

} // namespace cpplox
