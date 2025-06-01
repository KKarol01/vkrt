#pragma once

#ifndef NDEBUG
#include <string>
#include <cassert>
#include <fmt/format.h>

#define ENG_ERROR(str, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        fmt::println("[ERROR][{} : {}]" str, __FILE__, __LINE__, __VA_ARGS__);                                         \
        assert(false);                                                                                                 \
    }                                                                                                                  \
    while(0)

#define ENG_WARN(str, ...) fmt::println("[WARN][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__)

#define ENG_LOG(str, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        const std::string format = fmt::format("[LOG][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__);               \
        if(Engine::get().msg_log.size() >= 512) { Engine::get().msg_log.pop_back(); }                                  \
        fmt::println("{}", format);                                                                                    \
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

#define ENG_TODO() fmt::println("[TODO][{} : {}]", __FILE__, __LINE__)
#else
#define ENG_ERROR(str, ...)
#define ENG_WARN(str, ...)
#define ENG_LOG(str, ...)
#define ENG_WARN_ASSERT(str, ...)
#define ENG_TODO()
#endif