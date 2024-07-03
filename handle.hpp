#pragma once

#include <cstdint>

template<typename T> struct Handle {
	constexpr Handle() = default;
	constexpr explicit Handle(const T* ptr) noexcept : _handle(reinterpret_cast<std::uintptr_t>(ptr)) { } 

	std::uintptr_t _handle;
};