# Example game/app executable built on top of the eng static library.
# An external game project does the same thing (see README.md, "Using the engine from a game project").
add_executable(sandbox
    "main.cpp"
    "app/app.cpp"
)
target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sandbox PRIVATE eng::eng)
if(MSVC)
    target_compile_options(sandbox PRIVATE /W4)
endif()

# Keep the exe directly in the build root (out/) like the old 'eng' executable, so the engine's
# asset-root search (cwd, ../, ../../) still finds the repo's assets/ folder when run from there.
set_target_properties(sandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_NAME_DEBUG "sandboxDebug"
    RUNTIME_OUTPUT_NAME_RELEASE "sandbox"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# The sandbox uses the engine repo's own assets/ (which already contains assets/shaders), so no ASSETS_DIR here.
eng_setup_executable(sandbox)
