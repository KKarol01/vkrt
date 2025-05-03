cmake_minimum_required(VERSION 3.29)
project(vkrt)

find_package(Vulkan REQUIRED)

add_subdirectory(third_party/glm SYSTEM)
target_compile_definitions(glm INTERFACE
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
set(FASTGLTF_USE_STD_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/fastgltf)

add_library(build_header_only STATIC
    "eng/build_header_only.cpp"
)
target_compile_definitions(build_header_only
    PRIVATE
        STB_IMAGE_IMPLEMENTATION
        STB_INCLUDE_IMPLEMENTATION
        VOLK_IMPLEMENTATION
        VMA_IMPLEMENTATION
        VMA_STATIC_VULKAN_FUNCTIONS=0
        VMA_DYNAMIC_VULKAN_FUNCTIONS=0
    PUBLIC
        STB_INCLUDE_LINE_GLSL
        VK_USE_PLATFORM_WIN32_KHR
        VK_VERSION_1_3
        WIN32_LEAN_AND_MEAN
)
target_include_directories(build_header_only
    PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party
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

add_library(eng_ui SHARED "eng/ui.cpp")
target_compile_definitions(eng_ui PRIVATE VK_NO_PROTOTYPES IMGUI_DEFINE_MATH_OPERATORS ENG_BUILD_AS_DLL)
target_compile_features(eng_ui PRIVATE cxx_std_23)
target_include_directories(eng_ui PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party ${Vulkan_INCLUDE_DIRS})
target_link_libraries(eng_ui PRIVATE build_header_only glm imgui imguizmo glfw)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_FUZZ OFF CACHE BOOL "" FORCE)
set(FMT_CUDA_TEST OFF CACHE BOOL "" FORCE)
set(FMT_OS ON CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(FMT_SYSTEM_HEADERS OFF CACHE BOOL "" FORCE)
set(FMT_UNICODE ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/fmt)

add_executable(vkrt 
    "eng/main.cpp"
    "eng/model_importer.cpp" 
    "eng/engine.cpp"  
    "eng/camera.cpp"
    "eng/scene.cpp"
    "eng/renderer/renderer_vulkan.cpp" 
    "eng/renderer/renderer_vulkan_wrappers.cpp"
    "eng/renderer/descpool.cpp"
    "eng/renderer/buffer.cpp"
    "eng/renderer/image.cpp"
    "eng/renderer/staging_buffer.cpp" 
    "eng/renderer/submit_queue.cpp"
    "eng/renderer/passes/rendergraph.cpp" 
    "eng/renderer/passes/passes.cpp" 
    "eng/renderer/pipeline.cpp"
    "eng/assets/importer.cpp"
)

target_include_directories(vkrt PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party ${Vulkan_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/third_party/fastgltf/include ${CMAKE_SOURCE_DIR}/third_party/fmt/include)
target_compile_features(vkrt PRIVATE cxx_std_23)
target_link_directories(vkrt PRIVATE ${Vulkan_INCLUDE_DIRS}/../Lib)
target_link_libraries(vkrt
    PRIVATE
    build_header_only
    vk-bootstrap
    glm 
    imgui
    imguizmo
    glfw
    fmt-header-only
    $<$<CONFIG:Debug>:shaderc_combinedd>
    $<$<CONFIG:Release>:shaderc_combined>
    fastgltf
    eng_ui
)
target_compile_definitions(vkrt
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
        VC_EXTRALEAN
        NOMINMAX
        ENGINE_BASE_ASSET_PATH="${CMAKE_SOURCE_DIR}/assets/"
        IMGUI_DEFINE_MATH_OPERATORS
)

target_precompile_headers(vkrt
    PRIVATE
        <vector>
        <string>
        <variant>
        <utility>
        <span>
        <cstddef>
        <forward_list>
        <queue>
        <atomic>
        <memory>
        <thread>
        <mutex>
        <numeric>
        <fstream>
        <volk/volk.h>
        <vulkan/vulkan.h>
        <glm/glm.hpp>
        <glm/mat3x3.hpp>
        <glm/gtc/quaternion.hpp>
        <glm/gtc/matrix_transform.hpp>
        <glm/gtx/quaternion.hpp>
        <glm/gtx/transform.hpp>
        <GLFW/glfw3.h>
        <imgui/imgui.h>
        <imgui/backends/imgui_impl_glfw.h>
        <imgui/backends/imgui_impl_vulkan.h>
        <VulkanMemoryAllocator/include/vk_mem_alloc.h>
        <vk-bootstrap/src/VkBootstrap.h>
        <shaderc/shaderc.hpp>
)