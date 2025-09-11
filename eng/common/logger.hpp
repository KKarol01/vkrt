#pragma once

#include <string>
#include <fmt/format.h>

#define ENG_FMT(str, ...) fmt::format(str, __VA_ARGS__)

#ifndef NDEBUG
#include <iostream>
#include <cassert>
#define ENG_PRT(str, ...) std::cout << ENG_FMT(str, __VA_ARGS__)
#define ENG_PRTLN(str, ...) ENG_PRT(str, __VA_ARGS__) << '\n'

#define ENG_ERROR(str, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        ENG_PRTLN("[ERROR][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__);                                          \
        assert(false);                                                                                                 \
    }                                                                                                                  \
    while(0)

#define ENG_WARN(str, ...) ENG_PRTLN("[WARN][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__)

#define ENG_LOG(str, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        const std::string format = ENG_FMT("[LOG][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__);                   \
        if(Engine::get().msg_log.size() >= 512) { Engine::get().msg_log.pop_back(); }                                  \
        ENG_PRTLN("{}", format);                                                                                       \
        Engine::get().msg_log.push_front(format);                                                                      \
    }                                                                                                                  \
    while(0)

#define ENG_WARN_ASSERT(str, ...)                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        ENG_WARN(str, __VA_ARGS__);                                                                                    \
        assert(false);                                                                                                 \
    }                                                                                                                  \
    while(0)

#define ENG_TODO(...) ENG_PRTLN("[TODO][{} : {}]", __FILE__, __LINE__)
#else
#ifdef _WIN32
#include <WinUser.h>
#define ENG_ERROR(str, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        MessageBoxA(NULL, ENG_FMT("[ERROR][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__).c_str(), NULL, MB_OK);    \
    }                                                                                                                  \
    while(0)
#endif
#define ENG_WARN(str, ...)
#define ENG_LOG(str, ...)
#define ENG_WARN_ASSERT(str, ...)
#define ENG_TODO(...)
#endif