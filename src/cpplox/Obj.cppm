export module cpplox:Obj;

import std;

import :EnumFormatter;

namespace cpplox {

export class Obj
{
public:
    enum class ObjType : std::uint8_t
    {
        BoundMethod,
        Class,
        Closure,
        Function,
        Instance,
        Native,
        String,
        Upvalue,
    };

public:
    virtual ~Obj() = default;

    [[nodiscard]] constexpr auto get_type() const -> ObjType { return m_type; }

protected:
    explicit Obj(ObjType type)
        : m_type(type)
    {
    }

private:
    ObjType m_type;
};

export class ObjString;
export class ObjUpvalue;
export class ObjFunction;
export class ObjClosure;
export class ObjNative;
export class ObjClass;
export class ObjInstance;
export class ObjBoundMethod;

} // namespace cpplox

template <>
struct std::formatter<cpplox::Obj::ObjType> : cpplox::EnumFormatter<cpplox::Obj::ObjType>
{
};
