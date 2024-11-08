cmake_minimum_required(VERSION 3.6)
project(vkrt)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED)

add_subdirectory(third_party/glm SYSTEM)
target_compile_definitions(glm INTERFACE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_AVX2
)

add_subdirectory(third_party/vk-bootstrap SYSTEM)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

set(VMA_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/VulkanMemoryAllocator)
add_library(vma STATIC build_vma.cpp)
target_compile_definitions(vma PUBLIC VK_USE_PLATFORM_WIN_32_KHR WIN32_LEAN_AND_MEAN VK_VERSION_1_3)
target_include_directories(vma PUBLIC ${CMAKE_SOURCE_DIR}/third_party/ ${Vulkan_INCLUDE_DIRS})

add_library(volk STATIC build_volk.c)
target_include_directories(volk PUBLIC ${CMAKE_SOURCE_DIR}/third_party ${Vulkan_INCLUDE_DIRS})
target_compile_definitions(volk
    PUBLIC
    VK_USE_PLATFORM_WIN32_KHR
)

add_library(stb STATIC build_stb.c)
target_include_directories(stb PUBLIC ${CMAKE_SOURCE_DIR}/third_party)

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

add_executable(vkrt 
    "main.cpp"
    "model_importer.cpp" 
    "engine.cpp"  
    "renderer_vulkan.cpp" 
    "camera.cpp"
    "gpu_staging_manager.cpp"
    "ui.cpp" 
    "scene.cpp"
    "renderer_vulkan_wrappers.cpp"   
)

target_include_directories(vkrt PRIVATE ${Vulkan_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/third_party/fastgltf/include)
target_compile_features(vkrt PRIVATE cxx_std_23)
target_link_directories(vkrt PRIVATE ${Vulkan_INCLUDE_DIRS}/../Lib)
target_link_libraries(vkrt
    PRIVATE
    glm
    vk-bootstrap
    glfw
    vma
    volk
    stb
    imgui
    imguizmo
    $<$<CONFIG:Debug>:shaderc_combinedd>
    $<$<CONFIG:Release>:shaderc_combined>
    fastgltf
)
target_compile_definitions(vkrt
    PRIVATE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    VC_EXTRALEAN
    NOMINMAX
    ENGINE_BASE_ASSET_PATH="${CMAKE_SOURCE_DIR}/assets/"
)
target_precompile_headers(vkrt
    PRIVATE
    <volk/volk.h>
    vulkan_structs.hpp
    <GLFW/glfw3.h>
    <glm/glm.hpp>
    <VulkanMemoryAllocator/include/vk_mem_alloc.h>
    <vk-bootstrap/src/VkBootstrap.h>
    <shaderc/shaderc.hpp>
    <fastgltf/core.hpp>
    <fastgltf/types.hpp>
    <fastgltf/tools.hpp>
    <fastgltf/glm_element_traits.hpp>
    <iostream>
    <format>
)