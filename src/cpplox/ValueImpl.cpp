module cpplox;

import std;

import :Environment;
import :Value;

namespace cpplox {

auto ValueTypes::Callable::bind(Value value) const -> ValueTypes::Callable
{
    auto this_closure = std::make_shared<Environment>(closure.get());
    this_closure->define("this", std::move(value));
    return ValueTypes::Callable{.closure = std::move(this_closure), .func = func};
}

} // namespace cpplox
