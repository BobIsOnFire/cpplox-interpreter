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

export using ScannerPtr = std::unique_ptr<IScanner>;

export auto make_scanner(std::string_view source) -> ScannerPtr;

} // namespace cpplox
