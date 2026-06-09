cmake_minimum_required(VERSION 3.29)
project(eng)

find_package(Vulkan REQUIRED)

add_subdirectory(third_party)

add_library(build_header_only STATIC
    "eng/build_header_only.cpp"
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
        ${CMAKE_SOURCE_DIR}/third_party
        ${Vulkan_INCLUDE_DIRS}
)

add_executable(eng
    "main.cpp"
    "app/app.cpp"

    "eng/engine.cpp"  
	"eng/fs/fs.cpp"
    "eng/camera.cpp"
    "eng/scene.cpp"
	"eng/ui/ui.cpp" 
    "eng/to_string.cpp" 
	"eng/ecs/components.cpp"
    "eng/physics/bvh.cpp"
    "eng/renderer/renderer.cpp"
    "eng/renderer/imgui/imgui_renderer.cpp"
    "eng/renderer/vulkan/to_vk.cpp"
    "eng/renderer/vulkan/vulkan_backend.cpp" 
    "eng/renderer/bindlesspool.cpp"
    "eng/renderer/staging_buffer.cpp" 
    "eng/renderer/submit_queue.cpp"
	"eng/renderer/passes/renderpass.cpp"
	"eng/renderer/rendergraph.cpp"
	"eng/assets/asset_manager.cpp"
	"eng/assets/loaders.cpp" 
	"eng/assets/serialization.cpp"
 "eng/renderer/mesh/mesh_renderer.cpp")

target_include_directories(eng 
	PRIVATE 
	${CMAKE_SOURCE_DIR} 
	${CMAKE_SOURCE_DIR}/third_party 
	${Vulkan_INCLUDE_DIRS} 
	${CMAKE_SOURCE_DIR}/third_party/fastgltf/include 
	${CMAKE_SOURCE_DIR}/third_party/fmt/include
	${CMAKE_SOURCE_DIR}/third_party/reproc/reproc++/include
	${CMAKE_SOURCE_DIR}/third_party/zlib/include/
)
target_compile_features(eng PRIVATE cxx_std_23)
target_link_directories(eng PRIVATE ${CMAKE_SOURCE_DIR}/third_party ${Vulkan_INCLUDE_DIRS}/../Lib)
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
set_target_properties(eng PROPERTIES 
    RUNTIME_OUTPUT_NAME_DEBUG "${CMAKE_PROJECT_NAME}Debug"
    RUNTIME_OUTPUT_NAME_RELEASE "${CMAKE_PROJECT_NAME}"
)
target_link_libraries(eng
    PUBLIC
        glm 
        imgui
        imguizmo
        fmt::fmt
    PRIVATE
        build_header_only
        vk-bootstrap
        glfw
        fastgltf
        meshoptimizer
		reproc++
		zlib/lib/z
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
        <eng/common/handleflatset.hpp>
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

add_custom_command(TARGET eng POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/zlib/lib/z.dll"
        "${CMAKE_SOURCE_DIR}/third_party/zlib/lib/z.exp"
        $<TARGET_FILE_DIR:eng>
)