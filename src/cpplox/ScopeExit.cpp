module cpplox:ScopeExit;

import std;

namespace cpplox {

template <typename T>
concept ScopeExitFn = requires(T && f) {
    { std::invoke(std::forward<T>(f)) } -> std::same_as<void>;
};

export class ScopeExit
{
public:
    template <ScopeExitFn Func>
    explicit ScopeExit(Func && m_func)
        : m_func(std::forward<Func>(m_func))
    {
    }

    ScopeExit(ScopeExit && other) noexcept
        : m_func(std::move(other.m_func))
    {
        other.release();
    }

    auto operator=(ScopeExit && other) noexcept -> ScopeExit &
    {
        if (this == &other) {
            return *this;
        }

        m_func = std::move(other.m_func);
        other.release();
        return *this;
    }

    ~ScopeExit()
    {
        if (m_func) {
            m_func();
        }
    }

    void release() noexcept { m_func = nullptr; }

    ScopeExit(const ScopeExit &) = delete;
    void operator=(const ScopeExit &) = delete;

private:
    std::function<void()> m_func;
};

} // namespace cpplox
