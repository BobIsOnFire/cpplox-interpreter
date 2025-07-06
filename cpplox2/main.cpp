import std;
import cpplox2;

auto main() -> int
{
    cpplox::Chunk chunk;

    // NOLINTBEGIN(readability-magic-numbers)
    auto val = cpplox::add_constant(chunk, 1.2);
    cpplox::write_chunk(chunk, cpplox::OpCode::Constant, {.line = 123, .column = 8});
    cpplox::write_chunk(chunk, val, {.line = 123, .column = 8});

    auto val2 = cpplox::add_constant(chunk, 3.4);
    cpplox::write_chunk(chunk, cpplox::OpCode::Constant, {.line = 123, .column = 14});
    cpplox::write_chunk(chunk, val2, {.line = 123, .column = 14});

    cpplox::write_chunk(chunk, cpplox::OpCode::Add, {.line = 123, .column = 12});

    auto val3 = cpplox::add_constant(chunk, 5.6);
    cpplox::write_chunk(chunk, cpplox::OpCode::Constant, {.line = 123, .column = 20});
    cpplox::write_chunk(chunk, val3, {.line = 123, .column = 20});

    cpplox::write_chunk(chunk, cpplox::OpCode::Divide, {.line = 123, .column = 18});

    cpplox::write_chunk(chunk, cpplox::OpCode::Negate, {.line = 123, .column = 7});

    cpplox::write_chunk(chunk, cpplox::OpCode::Return, {.line = 123, .column = 1});
    // NOLINTEND(readability-magic-numbers)

    // cpplox::disassemble_chunk(chunk, "test chunk");
    [[maybe_unused]] auto result = cpplox::interpret(chunk);
}
