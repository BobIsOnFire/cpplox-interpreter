export module cpplox:ObjReferenceTracer;

import std;

import :Obj;
import :Value;

namespace cpplox {

export class ObjReferenceTracer
{
public:
    struct Settings
    {
        bool debug_log_gc = false;
    };

    explicit ObjReferenceTracer(Settings settings)
        : m_settings(settings)
    {
    }

    auto trace(Obj * obj) -> void;
    auto trace(Value value) -> void;
    auto is_referenced(Obj * obj) const -> bool;

private:
    auto gc_log(Obj * obj, std::string_view action) const -> void;
    auto mark(Obj * obj) -> void;
    auto mark(const Value & value) -> void;
    auto trace_references(Obj * obj) -> void;

private:
    std::stack<Obj *> m_to_trace;
    std::unordered_set<Obj *> m_referenced;
    Settings m_settings;
};

} // namespace cpplox
