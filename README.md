Requires: C++23, CMake, Vulkan 1.3 support (with various extensions) and hardware raytracing. \
Building:
- Clone with: \
 `git clone https://github.com/KKarol01/vkrt.git --recurse-submodules --shallow-submodules --jobs 12`
- Open with Visual Studio (22) and CMake extension installed
- Compile

## Layout
- `include/eng/...` – public engine headers (`#include <eng/...>`)
- `src/...` – engine sources, built as the static library `eng` (alias `eng::eng`)
- `assets/shaders/...` – default engine shaders (HLSL, compiled at runtime with `dxc.exe` from `PATH`); `*.shared.hlsli` are also included from C++ as `<assets/shaders/...>`
- `examples/sandbox/` – example executable (`main.cpp` + `app/`) linking `eng`; built only when eng is the top-level project (`ENG_BUILD_EXAMPLES`)
- `third_party/` – submodules (+ prebuilt zlib)

## Using the engine from a game project
Add the engine as a subdirectory (e.g. a git submodule at `external/vkrt`, cloned with `--recurse-submodules`):
```cmake
cmake_minimum_required(VERSION 3.29)
project(mygame CXX)
add_subdirectory(external/vkrt)          # defines eng::eng and eng_setup_executable()

add_executable(mygame src/main.cpp)
target_link_libraries(mygame PRIVATE eng::eng)   # brings include paths, C++23, defines, transitive libs
# copies z.dll next to the exe, reuses eng's PCH list, and mirrors engine shaders into mygame/assets/shaders
eng_setup_executable(mygame ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
set_target_properties(mygame PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```
`main` follows `examples/sandbox/main.cpp`: `eng::get_engine().init(argc, argv)`, set up your game, `start()`, `destroy()`.

### Asset root
The engine has a single runtime asset root. At startup `FileSystem::init` looks for a directory named `assets`
in the working directory, then `../`, then `../../`, and maps virtual paths `/assets/...` onto `<root>/assets/...`.
That root holds both the game's assets (`/assets/assets0.engb`, models, ...) and the engine's default shaders
(`/assets/shaders/imgui/...`, `/assets/shaders/ssao/...`, ...), and it is passed to DXC as `-I <root>`. So a game must:
1. keep its assets in `<game>/assets/`,
2. have the engine's `assets/shaders/` present at `<game>/assets/shaders/` (`eng_setup_executable(... ASSETS_DIR ...)` copies them after every build; add that folder to the game's `.gitignore`, and put game shaders in their own subfolder to avoid clashes),
3. run with the working directory at most two levels below `<game>/` (e.g. set `VS_DEBUGGER_WORKING_DIRECTORY` as above).

`find_package`/install exports are not provided: the engine's dependencies (imgui, imguizmo, build_header_only, prebuilt zlib, submodules) are plain in-tree targets, so `add_subdirectory` is the supported way to consume it.
