module cpplox;

import std;

import :Environment;
import :Value;

namespace cpplox {

auto ValueTypes::Object::get_method(const std::string & name) -> std::optional<ValueTypes::Callable>
{
    const auto * method = m_class.find_method(name);
    if (method != nullptr) {
        auto this_closure = std::make_shared<Environment>(method->closure.get());
        this_closure->define("this", *this);
        return ValueTypes::Callable{.closure = std::move(this_closure), .func = method->func};
    }

    return std::nullopt;
}

} // namespace cpplox
