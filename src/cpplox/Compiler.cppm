export module cpplox:Compiler;

import std;

import :Object;

namespace cpplox {

export auto compile(std::string_view source) -> ObjFunction *;

} // namespace cpplox