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
    explicit Obj(ObjType type)
        : m_type(type)
    {
    }

    virtual ~Obj() = default;

    [[nodiscard]] constexpr auto get_type() const -> ObjType { return m_type; }

    // TODO: Should probably be virtual instead of having out-of-line mark_object()
    constexpr auto mark() -> void { m_marked = true; }
    constexpr auto clear_mark() -> void { m_marked = false; }
    [[nodiscard]] constexpr auto is_marked() const -> bool { return m_marked; }

private:
    ObjType m_type;
    bool m_marked = false;
};

export class ObjString;
export class ObjUpvalue;
export class ObjFunction;
export class ObjClosure;
export class ObjNative;
export class ObjClass;
export class ObjInstance;
export class ObjBoundMethod;

export auto release_object(Obj * obj) -> void;

} // namespace cpplox

template <>
struct std::formatter<cpplox::Obj::ObjType> : cpplox::EnumFormatter<cpplox::Obj::ObjType>
{
};
