#pragma once

#include <string>
#include <eng/string/stack_string.hpp>
#include <fmt/format.h>

#define ENG_FMT(str, ...) fmt::format(str, __VA_ARGS__)
#define ENG_FMT_STR(str, ...) ENG_FMT(fmt::runtime(str), __VA_ARGS__)

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
        ENG_BREAKPOINT();                                                                                              \
        ENG_LOG(__VA_ARGS__);                                                                                          \
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
};

thread_local inline std::deque<ScopedTimer> scoped_timers;

} // namespace eng

#define ENG_TIMER_START(msg, ...) ::eng::scoped_timers.emplace_back(ENG_FMT(msg, __VA_ARGS__));
#define ENG_TIMER_END() ::eng::scoped_timers.pop_back();

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