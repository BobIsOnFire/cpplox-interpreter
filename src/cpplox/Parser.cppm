export module cpplox:Parser;

import std;

import :Stmt;

namespace cpplox {

export auto parse(std::string_view source) -> std::optional<std::vector<StmtPtr>>;

} // namespace cpplox
