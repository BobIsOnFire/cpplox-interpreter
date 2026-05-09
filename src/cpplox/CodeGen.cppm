export module cpplox:CodeGen;

import std;

import :Code;

namespace cpplox {

export auto codegen(std::string_view source) -> std::optional<std::vector<Code>>;

} // namespace cpplox
