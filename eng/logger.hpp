#pragma once

#ifndef NDEBUG
#include <string>
#include <cassert>
#include <fmt/format.h>

#define ENG_ERROR(str, ...)                                                                                            \
    do {                                                                                                               \
        fmt::println("[ERROR][{} : {}]" str, __FILE__, __LINE__, __VA_ARGS__);                                         \
        assert(false);                                                                                                 \
    } while(0)

#define ENG_WARN(str, ...) fmt::println("[WARN][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__)

#define ENG_LOG(str, ...)                                                                                              \
    do {                                                                                                               \
        const std::string format = fmt::format("[LOG][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__);               \
        if(Engine::get().msg_log.size() >= 512) { Engine::get().msg_log.pop_back(); }                                  \
        Engine::get().msg_log.push_front(format);                                                                      \
    } while(0)

#define ENG_ASSERT(expr, str, ...)                                                                                     \
    if(!(expr)) {                                                                                                      \
        fmt::println("[ASSERT][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__);                                      \
        assert(false);                                                                                                 \
    }

#define ENG_TODO(str, ...) fmt::println("[TODO][{} : {}]: " str, __FILE__, __LINE__, __VA_ARGS__)
#else
#define ENG_ERROR(str, ...)
#define ENG_WARN(str, ...)
#define ENG_LOG(str, ...)
#define ENG_ASSERT(expr, str, ...)
#define ENG_TODO(str, ...)
#endif