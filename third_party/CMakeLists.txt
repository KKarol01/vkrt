cmake_minimum_required(VERSION 3.29)

add_subdirectory(glm SYSTEM)
target_compile_definitions(glm PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_FORCE_AVX2
    GLM_ENABLE_EXPERIMENTAL
)

add_subdirectory(vk-bootstrap SYSTEM)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

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
add_subdirectory(fastgltf)

set(REPROC++ ON CACHE BOOL "" FORCE)
set(REPROC_TEST OFF CACHE BOOL "" FORCE)
set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(reproc)

add_library(imgui STATIC
    "imgui/imgui.cpp"
    "imgui/imgui_draw.cpp"
    "imgui/imgui_tables.cpp"
    "imgui/imgui_widgets.cpp"
    "imgui/backends/imgui_impl_glfw.cpp"
    "imgui/backends/imgui_impl_vulkan.cpp"
 )
target_include_directories(imgui 
    PUBLIC
        imgui 
        volk
    PRIVATE 
        imgui/backends 
        ${Vulkan_INCLUDE_DIRS} 
        glfw/include
)
target_compile_definitions(imgui
    PRIVATE
        IMGUI_IMPL_VULKAN_USE_VOLK
)

add_library(imguizmo STATIC
    "imguizmo/src/imguizmo.cpp"
 )
target_include_directories(imguizmo
    PRIVATE
        "imgui"
)

set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_FUZZ OFF CACHE BOOL "" FORCE)
set(FMT_CUDA_TEST OFF CACHE BOOL "" FORCE)
set(FMT_OS ON CACHE BOOL "" FORCE)
set(FMT_MODULE OFF CACHE BOOL "" FORCE)
set(FMT_SYSTEM_HEADERS OFF CACHE BOOL "" FORCE)
set(FMT_UNICODE OFF CACHE BOOL "" FORCE)
add_subdirectory(fmt)

set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(meshoptimizer)