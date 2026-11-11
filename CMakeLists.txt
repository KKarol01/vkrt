cmake_minimum_required(VERSION 3.29)
project(eng LANGUAGES C CXX)

option(ENG_BUILD_EXAMPLES "Build the engine example/sandbox executable" ${PROJECT_IS_TOP_LEVEL})

find_package(Vulkan REQUIRED)

add_subdirectory(third_party/glm SYSTEM)
target_compile_definitions(glm PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_AVX2
    GLM_ENABLE_EXPERIMENTAL
)

add_subdirectory(third_party/vk-bootstrap SYSTEM)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

set(FASTGLTF_USE_CUSTOM_SMALLVECTOR ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_GLTF_RS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_ASSIMP OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DEPRECATED_EXT OFF CACHE BOOL "" FORCE)
set(FASTGLTF_DISABLE_CUSTOM_MEMORY_POOL OFF CACHE BOOL "" FORCE)
set(FASTGLTF_USE_64BIT_FLOAT OFF CACHE BOOL "" FORCE)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_CPP_MODULES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_USE_STD_MODULE ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/fastgltf)

set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/reproc)

add_library(build_header_only STATIC
    "src/build_header_only.cpp"
 )
target_compile_definitions(build_header_only
    PRIVATE
        STB_IMAGE_IMPLEMENTATION
		STB_IMAGE_WRITE_IMPLEMENTATION
        VOLK_IMPLEMENTATION
        VMA_IMPLEMENTATION
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=0
    PUBLIC
        $<IF:$<STREQUAL:$<PLATFORM_ID>,Windows>, VK_USE_PLATFORM_WIN32_KHR, VK_USE_PLATFORM_XCB_KHR>
        VK_VERSION_1_3
        WIN32_LEAN_AND_MEAN
)

target_include_directories(build_header_only
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${Vulkan_INCLUDE_DIRS}
)

add_library(imgui STATIC
    "third_party/imgui/imgui.cpp"
    "third_party/imgui/imgui_draw.cpp"
    "third_party/imgui/imgui_tables.cpp"
    "third_party/imgui/imgui_widgets.cpp"
    "third_party/imgui/backends/imgui_impl_glfw.cpp"
    "third_party/imgui/backends/imgui_impl_vulkan.cpp"
 )
target_include_directories(imgui 
    PUBLIC
        third_party/imgui 
        third_party/volk
    PRIVATE 
        third_party/imgui/backends 
        ${Vulkan_INCLUDE_DIRS} 
        third_party/glfw/include
)
target_compile_definitions(imgui
    PRIVATE
        IMGUI_IMPL_VULKAN_USE_VOLK
)

add_library(imguizmo STATIC
    "third_party/ImGuizmo/ImGuizmo.cpp"
 )
target_include_directories(imguizmo
    PRIVATE
        "third_party/imgui"
)

#add_library(eng_ui SHARED "eng/ui.cpp")
#target_compile_definitions(eng_ui PRIVATE VK_NO_PROTOTYPES IMGUI_DEFINE_MATH_OPERATORS ENG_BUILD_AS_DLL)
#target_compile_features(eng_ui PRIVATE cxx_std_23)
#target_include_directories(eng_ui PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party ${Vulkan_INCLUDE_DIRS})
#target_link_libraries(eng_ui PRIVATE build_header_only glm imgui imguizmo glfw)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_FUZZ OFF CACHE BOOL "" FORCE)
set(FMT_CUDA_TEST OFF CACHE BOOL "" FORCE)
set(FMT_OS ON CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(FMT_SYSTEM_HEADERS OFF CACHE BOOL "" FORCE)
set(FMT_UNICODE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fmt)

set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/meshoptimizer)

# zlib ships prebuilt in third_party/zlib (z.lib import library + z.dll).
# Wrapped in an imported target so the link path is absolute and propagates to consumers of the static eng lib.
add_library(eng_zlib SHARED IMPORTED GLOBAL)
set_target_properties(eng_zlib PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/lib/z.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/lib/z.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib/include"
)

add_library(eng STATIC
    "src/engine.cpp"
    "src/fs/fs.cpp"
    "src/camera.cpp"
    "src/scene.cpp"
    "src/ui/ui.cpp"
    "src/to_string.cpp"
    "src/ecs/components.cpp"
    "src/physics/bvh.cpp"
    "src/renderer/renderer.cpp"
    "src/renderer/imgui/imgui_renderer.cpp"
    "src/renderer/vulkan/to_vk.cpp"
    "src/renderer/vulkan/vulkan_backend.cpp"
    "src/renderer/bindlesspool.cpp"
    "src/renderer/staging_buffer.cpp"
    "src/renderer/submit_queue.cpp"
    "src/renderer/rendergraph.cpp"
    "src/renderer/mesh/mesh_renderer.cpp"
    "src/assets/asset_manager.cpp"
    "src/assets/loaders.cpp"
    "src/assets/serialization.cpp"
)
add_library(eng::eng ALIAS eng)

target_include_directories(eng
    PUBLIC
        # <eng/...> public headers
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        # engine root: public headers include shared C++/HLSL headers as <assets/shaders/....hlsli>
        ${CMAKE_CURRENT_SOURCE_DIR}
        # public headers include <volk/...>, <VulkanMemoryAllocator/...>, <glm/...>, <imgui/...> etc.
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${Vulkan_INCLUDE_DIRS}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fastgltf/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fmt/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/reproc/reproc++/include
)
target_compile_features(eng PUBLIC cxx_std_23)
target_link_directories(eng PUBLIC ${Vulkan_INCLUDE_DIRS}/../Lib)
if(MSVC)
    target_compile_options(eng PRIVATE
        /W4
    )
else()
    target_compile_options(eng PRIVATE
        -Wall -Wextra -Werror -Wpedantic
        -Wno-error=unused-parameter -Wno-error=unused-variable -Wno-error=unused-lambda-capture
    )
endif()
# Static lib: PRIVATE deps are still propagated to the final executable's link line ($<LINK_ONLY:...>).
target_link_libraries(eng
    PUBLIC
        glm
        imgui
        imguizmo
        fmt::fmt
        eng_zlib
        # PUBLIC so its VK_USE_PLATFORM_* / VK_VERSION_1_3 / WIN32_LEAN_AND_MEAN defines reach code including eng headers
        build_header_only
    PRIVATE
        vk-bootstrap
        glfw
        fastgltf
        meshoptimizer
        reproc++
)
target_compile_definitions(eng
    PUBLIC
        _CRT_SECURE_NO_WARNINGS
        VC_EXTRALEAN
        NOMINMAX
        IMGUI_DEFINE_MATH_OPERATORS
        ImTextureID_Invalid=~0ull
        $<$<CONFIG:Debug>:ENG_DEBUG_BUILD>
        $<$<PLATFORM_ID:Windows>:ENG_PLATFORM_WIN32>
)

target_precompile_headers(eng
    PRIVATE
        <algorithm>
        <atomic>
        <cstddef>
        <deque>
        <forward_list>
        <fstream>
        <functional>
        <map>
        <memory>
        <mutex>
        <numeric>
        <queue>
        <ranges>
        <set>
        <shared_mutex>
        <span>
        <string>
        <thread>
        <unordered_map>
        <unordered_set>
        <utility>
        <variant>
        <vector>
        
		<volk/volk.h>
        <vulkan/vulkan.h>
        <glm/glm.hpp>
        <glm/mat3x3.hpp>
        <glm/gtc/quaternion.hpp>
        <glm/gtc/matrix_transform.hpp>
        <glm/gtx/quaternion.hpp>
        <glm/gtx/transform.hpp>
        <imgui/imgui.h>
        <imgui/backends/imgui_impl_glfw.h>
        <imgui/backends/imgui_impl_vulkan.h>
        <VulkanMemoryAllocator/include/vk_mem_alloc.h>
        <vk-bootstrap/src/VkBootstrap.h>
        <fmt/format.h>
        
		<eng/assets/serialization.hpp>
		<eng/common/flags.hpp>
        <eng/common/handle.hpp>
        <eng/common/pool.hpp>
        <eng/common/uniquepool.hpp>
        <eng/common/hash.hpp>
        <eng/common/logger.hpp>
        <eng/common/sparseset.hpp>
        <eng/common/to_string.hpp>
        <eng/common/types.hpp>
        <eng/common/scalar_types.hpp>
        <eng/ecs/ecs.hpp>
        <eng/string/stack_string.hpp>

        <eng/renderer/renderer_fwd.hpp>
        <eng/renderer/vulkan/to_vk.hpp>
)

# Call on a game/app executable that links eng:
#   eng_setup_executable(<target> [ASSETS_DIR <dir>])
# - reuses eng's precompiled-header list for the target (code written against eng has historically relied on it),
# - copies runtime DLLs (z.dll) next to the executable after each build,
# - if ASSETS_DIR is given, mirrors the engine's default shaders into <ASSETS_DIR>/shaders after each build,
#   because the engine resolves '/assets/shaders/...' against the single runtime asset root (see README).
function(eng_setup_executable target)
    cmake_parse_arguments(ARG "" "ASSETS_DIR" "" ${ARGN})
    get_target_property(_eng_pch eng PRECOMPILE_HEADERS)
    if(_eng_pch)
        target_precompile_headers(${target} PRIVATE ${_eng_pch})
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS
    )
    if(ARG_ASSETS_DIR)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets/shaders"
                "${ARG_ASSETS_DIR}/shaders"
        )
    endif()
endfunction()

if(ENG_BUILD_EXAMPLES)
    add_subdirectory(examples/sandbox)
endif()
