export module cpplox:SourceLocation;

import std;

import boost.container_hash;

namespace cpplox {

export struct SourceLocation
{
    std::size_t line;
    std::size_t column;
    // TODO: filename and function/method name
    // TODO: store range of source locations? for diagnostics maybe?

    auto operator==(const SourceLocation &) const -> bool = default;
};

} // namespace cpplox

template <>
struct std::hash<cpplox::SourceLocation>
{
    auto operator()(const cpplox::SourceLocation & sloc) const -> std::size_t
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, sloc.line);
        boost::hash_combine(seed, sloc.column);
        return seed;
    }
};
