#pragma once

#include <string>
#include <eng/string/stack_string.hpp>
#include <fmt/format.h>

#define ENG_FMT(str, ...) fmt::format(str, __VA_ARGS__)
#define ENG_FMT_STR(str, ...) ENG_FMT(fmt::runtime(str), __VA_ARGS__)
#define ENG_FMT_TO_N(output, max_count, str, ...) fmt::format_to_n(output, max_count, str, __VA_ARGS__).size

#ifdef ENG_DEBUG_BUILD
#include <iostream>
#include <cassert>
#define ENG_PRT(msg, ...) std::cout << ENG_FMT(msg, __VA_ARGS__)
#define ENG_PRTLN(msg, ...) ENG_PRT(msg, __VA_ARGS__) << '\n'

#if defined(__clang__)
#define ENG_BREAKPOINT __builtin_debugtrap
#elif defined(_MSC_VER)
#define ENG_BREAKPOINT __debugbreak
#else
#define ENG_BREAKPOINT []() { assert(false); }
#endif

#if defined(__clang__)
#define ENG_ASSERT(expr, ...)                                                                                          \
    if((bool)(expr) == false)                                                                                          \
    {                                                                                                                  \
        ENG_BREAKPOINT();                                                                                              \
        __VA_OPT__(ENG_LOG(__VA_ARGS__));                                                                              \
    }
#else
#define ENG_ASSERT(expr, ...)                                                                                          \
    if((bool)(expr) == false)                                                                                          \
    {                                                                                                                  \
        ENG_LOG(__VA_ARGS__);                                                                                          \
        ENG_BREAKPOINT();                                                                                              \
    }
#endif

#define ENG_ERROR(msg, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        ENG_PRTLN("[ERROR][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__);                                          \
        std::terminate();                                                                                              \
    }                                                                                                                  \
    while(0)

#define ENG_WARN(msg, ...) ENG_PRTLN("[WARN][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__)

#define ENG_LOG(msg, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        const std::string format = ENG_FMT("[LOG][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__);                   \
        /*if(get_engine().msg_log.size() >= 512) { get_engine().msg_log.pop_back(); } */                               \
        ENG_PRTLN("{}", format);                                                                                       \
        /*get_engine().msg_log.push_front(format); */                                                                  \
    }                                                                                                                  \
    while(0)

#define ENG_TODO(msg, ...) ENG_PRTLN("[TODO][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__)

namespace eng
{

struct ScopedTimer
{
    ScopedTimer(std::string_view label);
    ~ScopedTimer();
    StackString<64> label;
    double start_secs{};
    uint32_t nest_level{ ~0u };
};

thread_local inline std::deque<ScopedTimer> g_scoped_timers;

struct ScopedTimerProxy
{
    ScopedTimerProxy(std::string_view label) { g_scoped_timers.emplace_back(label); }
    ~ScopedTimerProxy() { g_scoped_timers.pop_back(); }
};

} // namespace eng

#define ENG_TIMER_EXPAND(a, b) a##b
#define ENG_TIMER_CONCAT_LINE(a, b) ENG_TIMER_EXPAND(a, b)
#define ENG_TIMER_SCOPED(msg, ...)                                                                                     \
    ::eng::ScopedTimerProxy ENG_TIMER_CONCAT_LINE(eng_scoped_timer_proxy_, ENG_TIMER_EXPAND(__LINE__)){ ENG_FMT(msg, __VA_ARGS__) };
#define ENG_TIMER_START(msg, ...) ::eng::g_scoped_timers.emplace_back(ENG_FMT(msg, __VA_ARGS__));
#define ENG_TIMER_END() ::eng::g_scoped_timers.pop_back();

#else
#ifdef ENG_PLATFORM_WIN32
#include <WinUser.h>
#define ENG_ERROR(msg, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        MessageBoxA(NULL, ENG_FMT("[ERROR][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__).c_str(), NULL, MB_OK);    \
        std::terminate();                                                                                              \
    }                                                                                                                  \
    while(0)
#endif
#define ENG_WARN(msg, ...)
#define ENG_LOG(msg, ...)
#define ENG_TODO(...)
#define ENG_ASSERT(expr, ...)                                                                                          \
    if((bool)(expr) == false) { ENG_ERROR(__VA_ARGS__); }
#define ENG_TIMER_START(label)
#define ENG_TIMER_END()

#endif