cmake_minimum_required(VERSION 3.16)
project(idp-emu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set output directory for all builds
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# Set default build type to Debug if none specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (default Debug)" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Enable debug symbols and useful flags in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
    add_compile_options(-g -O0 -Wall -Wextra -Wno-unused-parameter)
endif()

# --- SDL2 (system) ---
find_package(SDL2 REQUIRED)

# --- OpenGL ---
find_package(OpenGL REQUIRED)

# --- Dear ImGui via FetchContent ---
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2 OpenGL::GL)

# Subdirectories
add_subdirectory(lib/chipsex)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
