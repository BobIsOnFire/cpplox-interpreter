export module cpplox:SourceLocation;

import std;

namespace cpplox {

export struct SourceLocation
{
    std::size_t line;
    std::size_t column;
    // TODO: filename and function/method name
};

} // namespace cpplox
