# Example game/app executable built on top of the eng static library.
# An external game project does the same thing (see README.md, "Using the engine from a game project").
add_executable(sandbox
    "main.cpp"
    "app/app.cpp"
)
target_include_directories(sandbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sandbox PRIVATE eng)
if(MSVC)
    target_compile_options(sandbox PRIVATE /W4)
endif()

# Keep the exe directly in the build root (out/). The asset root defaults to this repo (ENG_ROOT_DIR).
set_target_properties(sandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_NAME_DEBUG "sandboxDebug"
    RUNTIME_OUTPUT_NAME_RELEASE "sandbox"
    DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Copy runtime DLLs (z.dll) next to the exe.
add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:sandbox> $<TARGET_FILE_DIR:sandbox>
    COMMAND_EXPAND_LISTS
)
