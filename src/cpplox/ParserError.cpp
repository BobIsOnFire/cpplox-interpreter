module cpplox:ParserError;

import std;

namespace cpplox {

export class ParserError : public std::runtime_error
{
public:
    explicit ParserError(std::string_view what = "")
        : std::runtime_error(what.data())
    {
    }
};

} // namespace cpplox
