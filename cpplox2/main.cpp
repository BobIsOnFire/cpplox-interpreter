import std;
import cpplox2;

auto main() -> int
{
    cpplox::Chunk chunk;

    auto val = cpplox::add_constant(chunk, 1.2);
    cpplox::write_chunk(chunk, cpplox::OpCode::Constant, cpplox::SourceLocation{.line = 123});
    cpplox::write_chunk(chunk, val, cpplox::SourceLocation{.line = 123});

    cpplox::write_chunk(chunk, cpplox::OpCode::Return, cpplox::SourceLocation{.line = 123});

    cpplox::disassemble_chunk(chunk, "test chunk");
}
