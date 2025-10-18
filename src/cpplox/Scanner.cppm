export module cpplox:Scanner;

import std;

import :SourceLocation;
import :Token;

namespace cpplox {

export class IScanner
{
public:
    virtual ~IScanner() = default;
    virtual auto next_token() -> Token = 0;
};

export auto make_scanner(std::string_view source) -> std::unique_ptr<IScanner>;

} // namespace cpplox
