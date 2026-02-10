#pragma once

#include <string>
#include <fmt/format.h>

#define ENG_FMT(str, ...) fmt::format(str, __VA_ARGS__)
#define ENG_FMT_STR(str, ...) ENG_FMT(fmt::runtime(str), __VA_ARGS__)

#ifndef NDEBUG
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

#define ENG_ASSERT(expr, ...)                                                                                          \
    if((bool)(expr) == false) { ENG_BREAKPOINT(); }

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
        /*if(Engine::get().msg_log.size() >= 512) { Engine::get().msg_log.pop_back(); } */                             \
        ENG_PRTLN("{}", format);                                                                                       \
        /*Engine::get().msg_log.push_front(format); */                                                                 \
    }                                                                                                                  \
    while(0)

#define ENG_TODO(msg, ...) ENG_PRTLN("[TODO][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__)
#else
#ifdef _WIN32
#include <WinUser.h>
#define ENG_ERROR(msg, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        MessageBoxA(NULL, ENG_FMT("[ERROR][{} : {}]: " msg, __FILE__, __LINE__, __VA_ARGS__).c_str(), NULL, MB_OK);    \
    }                                                                                                                  \
    while(0)
#endif
#define ENG_WARN(msg, ...)
#define ENG_LOG(msg, ...)
#define ENG_TODO(...)
#define ENG_ASSERT(expr, ...)                                                                                          \
    if((bool)(expr) == false) { ENG_ERROR(__VA_ARGS__); }
#endif