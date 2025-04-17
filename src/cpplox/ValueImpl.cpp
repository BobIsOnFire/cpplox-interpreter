module cpplox;

import std;

import :Environment;
import :Value;

namespace cpplox {

auto ValueTypes::Callable::bind(Value value) const -> ValueTypes::Callable
{
    auto this_closure = std::make_shared<Environment>(m_closure.get());
    this_closure->define("this", std::move(value));
    return {std::move(this_closure), m_func};
}

} // namespace cpplox
