#pragma once

#ifndef NDEBUG
#include <print>
#include <string>
#include <cassert>

#define ENG_WARN(fmt, ...) std::println("[WARN][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__)

#define ENG_LOG(fmt, ...)                                                                                              \
    do {                                                                                                               \
        const std::string str = std::format("[LOG][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);                  \
        std::println("{}", str);                                                                                       \
        if(Engine::get().msg_log.size() >= 512) { Engine::get().msg_log.pop_back(); }                                  \
        Engine::get().msg_log.push_front(str);                                                                         \
    } while(0)

#define ENG_ASSERT(expr, fmt, ...)                                                                                     \
    if(!(expr)) {                                                                                                      \
        std::println("[ASSERT][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__);                                      \
        assert(false);                                                                                                 \
    }

#define ENG_TODO(fmt, ...) std::println("[TODO][{} : {}]: " fmt, __FILE__, __LINE__, __VA_ARGS__)
#else
#define ENG_WARN(fmt, ...)
#define ENG_LOG(fmt, ...)
#define ENG_ASSERT(expr, fmt, ...)
#define ENG_TODO(fmt, ...)
#endif