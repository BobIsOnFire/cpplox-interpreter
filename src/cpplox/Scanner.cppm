export module cpplox:Scanner;

import std;

import :SourceLocation;
import :Token;

namespace cpplox {

export struct Scanner
{
    std::string_view source;
    std::size_t start;
    std::size_t current;
    SourceLocation start_sloc;
    SourceLocation sloc;
};

export auto init_scanner(std::string_view source) -> void;
export auto scan_token() -> Token;

} // namespace cpplox
