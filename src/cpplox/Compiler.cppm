export module cpplox:Compiler;

import std;

import :Code;

namespace cpplox {

export auto compile(std::string_view source) -> std::optional<std::vector<Code>>;

} // namespace cpplox
