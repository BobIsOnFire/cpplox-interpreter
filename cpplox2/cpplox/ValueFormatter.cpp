export module cpplox2:ValueFormatter;

import :EnumFormatter;
import :Object;
import :Value;

import std;

template <> struct std::formatter<cpplox::ValueType> : cpplox::EnumFormatter<cpplox::ValueType>
{
};

template <> struct std::formatter<cpplox::ObjType> : cpplox::EnumFormatter<cpplox::ObjType>
{
};

template <> struct std::formatter<cpplox::Value> : std::formatter<std::string_view>
{
    auto format(const cpplox::Value & value, std::format_context & ctx) const
    {
        switch (value.get_type()) {
        case cpplox::ValueType::Boolean: return std::format_to(ctx.out(), "{}", value.as_boolean());
        case cpplox::ValueType::Nil: return std::format_to(ctx.out(), "nil");
        case cpplox::ValueType::Number: return std::format_to(ctx.out(), "{}", value.as_number());
        case cpplox::ValueType::Obj:
            switch (value.as_obj()->get_type()) {
            case cpplox::ObjType::String:
                return std::formatter<std::string_view>::format(value.as_string(), ctx);
            case cpplox::ObjType::Function: {
                auto name = value.as_objfunction()->get_name();
                if (name.empty()) {
                    return std::format_to(ctx.out(), "<script>");
                }
                return std::format_to(ctx.out(), "<fn {}>", name);
            }
            case cpplox::ObjType::Native: return std::format_to(ctx.out(), "<native fn>");
            }
        }
    }
};
