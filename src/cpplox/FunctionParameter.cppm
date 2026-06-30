export module cpplox:FunctionParameter;

import std;

import :Token;

namespace cpplox {

export struct FunctionParameter
{
    Token name;
    bool is_const = false;
};

} // namespace cpplox
