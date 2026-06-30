module;

#include <cassert>

module cpplox;

import std;

import :CodeGen;
import :Parser;

namespace cpplox {

class CodeGen
{
private:
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

    struct VariableInfo
    {
        OpCode get_op;
        OpCode set_op;
        Byte op_arg;
        bool is_const;
    };

public:
    explicit CodeGen(std::span<const StmtPtr> stmts)
        : m_stmts(stmts)
    {
        // FIXME: is it ok to pass a "default" synthetic token like this?
        init_function(synthetic_token({}, ""), FunctionType::Script);
    }

    [[nodiscard]] auto codegen() -> std::optional<std::vector<Code>>
    {
        for (const auto & stmt : m_stmts) {
            visit(stmt);
        }

        FunctionCompiler script_compiler = end_function();
        m_code.push_back(std::move(script_compiler.code));
        return m_had_errors ? std::nullopt : std::optional{std::move(m_code)};
    }

    auto operator()(auto && x) -> void { visit(x); }

private:
    auto visit(const StmtPtr & stmt) -> void
    {
        auto prev_op_sloc = m_op_sloc;
        m_op_sloc = stmt->sloc;
        std::visit(*this, stmt->stmt);
        m_op_sloc = prev_op_sloc;
    }

    auto visit(const ExprPtr & expr) -> void
    {
        auto prev_op_sloc = m_op_sloc;
        m_op_sloc = expr->sloc;
        std::visit(*this, expr->expr);
        m_op_sloc = prev_op_sloc;
    }

    auto visit(const stmt::Block & stmt) -> void
    {
        begin_scope();
        for (const auto & s : stmt.stmts) {
            visit(s);
        }
        end_scope();
    }

    auto visit(const stmt::Class & stmt) -> void
    {
        Byte name_constant = identifier_constant(stmt.name);
        declare_variable(stmt.name, /* is_const = */ true);

        emit_bytes(OpCode::Class, name_constant);
        define_variable(name_constant, /* is_const = */ true);

        m_class_compilers.push_back({
                .name = stmt.name.lexeme,
                .has_superclass = stmt.super.has_value(),
        });

        if (stmt.super.has_value()) {
            const auto & super = stmt.super.value();
            emit_variable_read(super.name);

            if (stmt.name.lexeme == super.name.lexeme) {
                error_at(super.name, "A class cannot inherit from itself.");
            }

            begin_scope();
            add_local(synthetic_token(super.name.sloc, "super"), /* is_const = */ true);
            define_variable(0, /* is_const = */ true);

            emit_variable_read(stmt.name);
            emit_byte(OpCode::Inherit);
        }

        emit_variable_read(stmt.name);

        for (const auto & method : stmt.methods) {
            Byte constant = identifier_constant(method.name);

            auto type = method.name.lexeme == "init" ? FunctionType::Initializer
                                                     : FunctionType::Method;
            function(type, method.name, method.params, method.stmts);

            emit_bytes(OpCode::Method, constant);
        }
        emit_byte(OpCode::Pop);

        if (current_class().has_superclass) {
            end_scope();
        }
    }

    auto visit(const stmt::Expression & stmt) -> void
    {
        visit(stmt.expr);
        emit_byte(OpCode::Pop);
    }

    auto visit(const stmt::Function & stmt) -> void
    {
        Byte global = parse_variable(stmt.name, /* is_const = */ true);
        mark_initialized();

        function(FunctionType::Function, stmt.name, stmt.params, stmt.stmts);

        define_variable(global, /* is_const = */ true);
    }

    auto visit(const stmt::If & stmt) -> void
    {
        visit(stmt.condition);

        std::size_t then_jump = emit_jump(OpCode::JumpIfFalseAndPop);

        visit(stmt.then_branch);

        std::size_t else_jump = emit_jump(OpCode::Jump);

        patch_jump(then_jump);

        if (stmt.else_branch.has_value()) {
            visit(stmt.else_branch.value());
        }

        patch_jump(else_jump);
    }

    auto visit(const stmt::Print & stmt) -> void
    {
        visit(stmt.expr);
        emit_byte(OpCode::Print);
    }

    auto visit(const stmt::Return & stmt) -> void
    {
        if (current_function().type == FunctionType::Script) {
            error_at(stmt.keyword, "Cannot return from top-level code.");
        }

        if (stmt.value.has_value()) {
            if (current_function().type == FunctionType::Initializer) {
                error_at(stmt.keyword, "Cannot return a value from initializer.");
            }

            visit(stmt.value.value());
            emit_byte(OpCode::Return);
        }
        else {
            emit_return();
        }
    }

    auto visit(const stmt::Var & stmt) -> void
    {
        // TODO: support const keyword
        bool is_const = false;

        Byte global = parse_variable(stmt.name, is_const);

        if (stmt.init.has_value()) {
            visit(stmt.init.value());
        }
        else {
            if (is_const) {
                error_at(stmt.name, "Const variable must have an initializer.");
            }
            emit_byte(OpCode::Nil);
        }

        define_variable(global, false);
    }

    auto visit(const stmt::While & stmt) -> void
    {
        current_function().loops.push_back(
                {.loop_start = current_code().code().size(), .loop_breaks = {}}
        );

        visit(stmt.condition);

        current_loop().loop_breaks.push_back(emit_jump(OpCode::JumpIfFalseAndPop));

        visit(stmt.body);
        emit_loop(current_loop().loop_start);

        for (const auto loop_break : current_loop().loop_breaks) {
            patch_jump(loop_break);
        }

        current_function().loops.pop_back();
    }

    auto visit(const expr::Assign & expr) -> void
    {
        visit(expr.value);

        auto var = resolve_variable(expr.name);
        if (var.is_const) {
            error_at(expr.op, "Cannot assign to const variable.");
        }
        emit_bytes(var.set_op, var.op_arg);
    }

    auto visit(const expr::Binary & expr) -> void
    {
        visit(expr.left);
        visit(expr.right);

        switch (expr.op.type) {
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
        default: error_at(expr.op, "Unknown binary operand.");
        }
    }

    auto visit(const expr::Call & expr) -> void
    {
        visit(expr.callee);
        for (const auto & arg : expr.args) {
            visit(arg);
        }
        emit_bytes(OpCode::Call, Byte(expr.args.size()));
    }

    auto visit(const expr::Get & expr) -> void
    {
        visit(expr.object);
        // TODO: Optimized invocations, ch. 28.5
        emit_bytes(OpCode::GetProperty, identifier_constant(expr.name));
    }

    auto visit(const expr::Grouping & expr) -> void { visit(expr.expr); }

    auto visit(const expr::Invalid & /* expr */) -> void
    {
        assert(false && "Never supposed to codegen on Invalid AST node, check error handling");
    }

    auto visit(const expr::Literal & expr) -> void
    {
        switch (expr.value.type) {
        case TokenType::False: emit_byte(OpCode::False); break;
        case TokenType::True: emit_byte(OpCode::True); break;
        case TokenType::Nil: emit_byte(OpCode::Nil); break;
        case TokenType::String: {
            auto lexeme = expr.value.lexeme;
            emit_constant(std::string{lexeme.substr(1, lexeme.length() - 2)});
            break;
        }
        case TokenType::Number: {
            auto lexeme = expr.value.lexeme;
            double value = 0;

            [[maybe_unused]] auto result = std::from_chars(lexeme.begin(), lexeme.end(), value);
            assert((result.ec == std::errc{}) && "Cannot parse Number token provided by Scanner");

            emit_constant(value);
            break;
        }
        default: error_at(expr.value, "Unknown literal.");
        }
    }

    auto visit(const expr::Logical & expr) -> void
    {
        visit(expr.left);

        switch (expr.op.type) {
        case TokenType::And: {
            std::size_t end_jump = emit_jump(OpCode::JumpIfFalse);
            emit_byte(OpCode::Pop);

            visit(expr.right);

            patch_jump(end_jump);
            break;
        }
        case TokenType::Or: {
            std::size_t else_jump = emit_jump(OpCode::JumpIfFalse);
            std::size_t end_jump = emit_jump(OpCode::Jump);

            patch_jump(else_jump);
            emit_byte(OpCode::Pop);

            visit(expr.right);

            patch_jump(end_jump);
            break;
        }
        default: error_at(expr.op, "Unknown logical operand.");
        }
    }

    auto visit(const expr::Set & expr) -> void
    {
        visit(expr.object);
        visit(expr.value);
        emit_bytes(OpCode::SetProperty, identifier_constant(expr.name));
    }

    auto visit(const expr::Super & expr) -> void
    {
        if (m_class_compilers.empty()) {
            error_at(expr.keyword, "Cannot use 'super' outside of a class.");
        }
        else if (!current_class().has_superclass) {
            error_at(expr.keyword, "Cannot use 'super' in a class with no superclass.");
        }

        // TODO: Faster super calls, ch. 29.3.2
        Byte name = identifier_constant(expr.method);
        emit_variable_read(synthetic_token(expr.keyword.sloc, "this"));
        emit_variable_read(synthetic_token(expr.keyword.sloc, "super"));
        emit_bytes(OpCode::GetSuper, name);
    }

    auto visit(const expr::This & expr) -> void
    {
        if (m_class_compilers.empty()) {
            error_at(expr.keyword, "Cannot use 'this' outside of a class.");
        }

        emit_variable_read(expr.keyword);
    }

    auto visit(const expr::Unary & expr) -> void
    {
        visit(expr.right);

        switch (expr.op.type) {
        case TokenType::Bang: emit_byte(OpCode::Not); break;
        case TokenType::Minus: emit_byte(OpCode::Negate); break;
        default: error_at(expr.op, "Unknown unary operand.");
        }
    }

    auto visit(const expr::Variable & expr) -> void { emit_variable_read(expr.name); }

    // *** Helpers ***

    [[nodiscard]] auto current_loop() -> LoopCompiler & { return current_function().loops.back(); }
    [[nodiscard]] auto current_function() -> FunctionCompiler &
    {
        return m_function_compilers.back();
    }
    [[nodiscard]] auto current_class() -> ClassCompiler & { return m_class_compilers.back(); }

    [[nodiscard]] auto current_code() -> Code & { return current_function().code; }

    auto error_at(const Token & token, std::string_view message) -> void
    {
        std::print(std::cerr, "[{}:{}] Error", token.sloc.line, token.sloc.column);

        if (token.type == TokenType::EndOfFile) {
            std::print(std::cerr, " at end");
        }
        else if (token.type != TokenType::Error) {
            std::print(std::cerr, " at '{}'", token.lexeme);
        }

        std::println(std::cerr, ": {}", message);

        m_had_errors = true;
    }

    // *** Byte Code Emitter ***

    template <typename T = std::monostate>
    using EmitResult = std::expected<T, std::string_view>;

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

    auto emit_loop(std::size_t start) -> EmitResult<>
    {
        emit_byte(OpCode::Loop);

        std::size_t offset = current_code().code().size() - start + 2;
        if (offset > DOUBLE_BYTE_MAX) {
            return std::unexpected("Loop body too large.");
        }

        emit_byte(static_cast<Byte>((offset >> BYTE_DIGITS) & BYTE_MAX));
        emit_byte(static_cast<Byte>(offset & BYTE_MAX));

        return {};
    }

    auto emit_jump(OpCode instruction) -> std::size_t
    {
        emit_bytes(instruction, BYTE_MAX, BYTE_MAX);

        return current_code().code().size() - 2;
    }

    auto patch_jump(std::size_t offset) -> EmitResult<>
    {
        std::size_t jump_length = current_code().code().size() - offset - 2;

        if (jump_length > DOUBLE_BYTE_MAX) {
            return std::unexpected("Too much code to jump over.");
        }

        current_code().code()[offset] = (jump_length >> BYTE_DIGITS) & BYTE_MAX;
        current_code().code()[offset + 1] = jump_length & BYTE_MAX;

        return {};
    }

    auto make_constant(CompiledValue value) -> EmitResult<Byte>
    {
        std::size_t c = current_code().add_constant(std::move(value));
        if (c >= BYTE_MAX) {
            return std::unexpected("Too many constants in one chunk.");
        }

        return Byte(c);
    }

    auto emit_constant(CompiledValue value) -> EmitResult<>
    {
        auto result = make_constant(std::move(value));
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        emit_bytes(OpCode::Constant, result.value());
        return {};
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

    auto function(
            FunctionType type,
            const Token & name,
            std::span<const Token> params,
            std::span<const StmtPtr> body
    ) -> void
    {
        init_function(name, type);
        begin_scope();

        current_function().code.arity() = params.size();
        for (const auto & param : params) {
            bool is_const = false; // TODO: support const params
            Byte constant = parse_variable(param, is_const);
            define_variable(constant, is_const);
        }

        for (const auto & stmt : body) {
            visit(stmt);
        }

        FunctionCompiler compiler = end_function();
        auto result = make_constant(
                FunctionReference{
                        .name = std::string{compiler.code.get_name()},
                        .sloc = compiler.code.get_location(),
                }
        );
        if (!result.has_value()) {
            error_at(name, result.error());
        }

        emit_bytes(OpCode::Closure, result.value());

        for (const auto & upvalue : compiler.upvalues) {
            emit_bytes(upvalue.is_local ? Byte(1) : Byte(0), upvalue.index);
        }
        m_code.push_back(std::move(compiler.code));
    }

    auto make_function(const Token & function, FunctionType type) -> FunctionCompiler
    {
        auto get_name = [function, type, this] -> std::string {
            // if (type == FunctionType::Anonymous) {
            //     return "[anonymous]";
            // }
            if (type == FunctionType::Function) {
                return std::string{function.lexeme};
            }
            if (type == FunctionType::Method || type == FunctionType::Initializer) {
                return std::format("{}.{}", current_class().name, function.lexeme);
            }
            return "";
        };

        return {
            .code = Code(get_name(), function.sloc),
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

    auto init_function(const Token & function, FunctionType type) -> void
    {
        m_function_compilers.push_back(make_function(function, type));
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
        auto result = make_constant(std::string{name.lexeme});
        if (!result.has_value()) {
            error_at(name, result.error());
            return 0;
        }
        return result.value();
    }

    auto synthetic_token(const SourceLocation & sloc, std::string_view name) -> Token
    {
        return {
                .type = TokenType::Identifier,
                .lexeme = name,
                .sloc = sloc,
        };
    }

    auto add_local(const Token & name, bool is_const) -> void
    {
        if (current_function().locals.size() > BYTE_MAX) {
            error_at(name, "Too many local variables in function.");
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
                    error_at(name, "Cannot read local variable in its own initializer.");
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

    auto add_upvalue(
            FunctionCompiler & compiler,
            const Token & name,
            Byte index,
            bool is_local,
            bool is_const
    ) -> std::pair<std::size_t, Upvalue &>
    {
        // TODO: use views::enumerate once it's available in libc++
        for (const auto & [idx, upvalue] :
             std::views::zip(std::views::iota(0UZ), compiler.upvalues)) {
            if (upvalue.index == index && upvalue.is_local == is_local) {
                return {idx, upvalue};
            }
        }

        if (compiler.upvalues.size() >= BYTE_MAX) {
            error_at(name, "Too many closure variables in function.");
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
                    current, name, static_cast<Byte>(idx), /* is_local = */ true, local.is_const
            );
        }

        auto maybe_upvalue = resolve_upvalue(comp_idx - 1, name);
        if (maybe_upvalue.has_value()) {
            auto & [idx, upvalue] = maybe_upvalue.value();
            return add_upvalue(
                    current, name, static_cast<Byte>(idx), /* is_local = */ false, upvalue.is_const
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

    auto declare_variable(const Token & name, bool is_const) -> void
    {
        if (!is_scope_local()) {
            return;
        }

        for (const auto & local : std::ranges::reverse_view{current_function().locals}) {
            if (local.depth != -1 && local.depth < current_function().scope_depth) {
                break;
            }

            if (local.name.lexeme == name.lexeme) {
                error_at(name, "Already a variable with this name in this scope.");
            }
        }

        add_local(name, is_const);
    }

    // FIXME: parse_variable is not a good name anymore, it doesn't parse anything
    auto parse_variable(const Token & name, bool is_const) -> Byte
    {
        declare_variable(name, is_const);

        if (is_scope_local()) {
            return 0;
        }

        return identifier_constant(name);
    }

    auto define_variable(Byte global, bool is_const) -> void
    {
        if (is_scope_local()) {
            mark_initialized();
            return;
        }
        emit_bytes(is_const ? OpCode::DefineGlobalConst : OpCode::DefineGlobalVar, global);
    }

    auto resolve_variable(const Token & name) -> VariableInfo
    {
        if (auto local_pos = resolve_local(name); local_pos.has_value()) {
            auto & [pos, local] = local_pos.value();
            return {
                    .get_op = OpCode::GetLocal,
                    .set_op = OpCode::SetLocal,
                    .op_arg = Byte(pos),
                    .is_const = local.is_const,
            };
        }

        if (auto upvalue_pos = resolve_upvalue(name); upvalue_pos.has_value()) {
            auto & [pos, upvalue] = upvalue_pos.value();
            return {
                    .get_op = OpCode::GetUpvalue,
                    .set_op = OpCode::SetUpvalue,
                    .op_arg = Byte(pos),
                    .is_const = upvalue.is_const,
            };
        }

        return {
                .get_op = OpCode::GetGlobal,
                .set_op = OpCode::SetGlobal,
                .op_arg = identifier_constant(name),
                // Global const assignment is checked at runtime
                .is_const = false,
        };
    }

    auto emit_variable_read(const Token & name) -> void
    {
        auto var = resolve_variable(name);
        emit_bytes(var.get_op, var.op_arg);
    }

private:
    std::vector<FunctionCompiler> m_function_compilers;
    std::vector<ClassCompiler> m_class_compilers;
    std::vector<Code> m_code;
    std::span<const StmtPtr> m_stmts;
    SourceLocation m_op_sloc = {.line = 1, .column = 1};
    bool m_had_errors = false;
};

auto codegen(std::string_view source) -> std::optional<std::vector<Code>>
{
    auto stmts = parse(source);
    if (!stmts.has_value()) {
        return std::nullopt;
    }

    CodeGen codegen(stmts.value());

    return codegen.codegen();
}

} // namespace cpplox
