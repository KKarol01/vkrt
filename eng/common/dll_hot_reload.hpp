#pragma once

#ifdef ENG_BUILD_AS_DLL
#ifdef _MSC_VER
#define ENG_API_CALL extern "C" __declspec(dllexport)
#elif __GNUC__
#define ENG_API_CALL extern "C" __attribute__((visibility("default")))
#endif
#define ENG_OVERRIDE_STD_NEW_DELETE(alloc_cbs)                                                                         \
    void* operator new(std::size_t size) { return alloc_cbs.alloc(size); }                                             \
    void operator delete(void* data) { alloc_cbs.free(data); }
#else
#define ENG_API_PFUNC(ret, name, ...) using eng_##name##_t = ret (*)(__VA_ARGS__)
#ifdef _MSC_VER
#define ENG_API_CALL extern "C" __declspec(dllimport)
#elif __GNUC__
#define ENG_API_CALL extern "C"
#endif
#endif