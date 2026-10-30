// SPDX-License-Identifier: LGPL-2.1-or-later
// Jigsmith shim for FreeCAD's Base::Console and Base::TimeElapsed, the only
// FreeCAD Base facilities planegcs uses. Messages go to a replaceable sink so the
// solver never writes to stdout behind the application's back.
#pragma once

#include <chrono>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace Base
{
class ConsoleSingleton
{
public:
    using Sink = std::function<void(int level, std::string_view message)>;

    static ConsoleSingleton& instance()
    {
        static ConsoleSingleton s;
        return s;
    }
    void setSink(Sink sink)
    {
        sink_ = std::move(sink);
    }

    template<typename... Args>
    void log(std::format_string<Args...> fmt, Args&&... args)
    {
        emit(0, std::format(fmt, std::forward<Args>(args)...));
    }
    template<typename... Args>
    void warning(std::format_string<Args...> fmt, Args&&... args)
    {
        emit(1, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    void emit(int level, const std::string& msg)
    {
        if (sink_) {
            sink_(level, msg);
        }
    }
    Sink sink_;
};

inline ConsoleSingleton& Console()
{
    return ConsoleSingleton::instance();
}

class TimeElapsed
{
public:
    TimeElapsed()
        : t_(std::chrono::steady_clock::now())
    {}
    static double diffTimeF(const TimeElapsed& a, const TimeElapsed& b)
    {
        return std::chrono::duration<double>(b.t_ - a.t_).count();
    }

private:
    std::chrono::steady_clock::time_point t_;
};
}  // namespace Base
