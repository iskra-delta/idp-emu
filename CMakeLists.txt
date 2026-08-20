cmake_minimum_required(VERSION 3.21)
set(IDP_VERSION "1.1.0" CACHE STRING "Release version embedded in packages")
project(idp-emu VERSION ${IDP_VERSION} LANGUAGES C CXX)

option(IDP_STATIC_RUNTIME
    "Link the compiler runtime statically where the platform supports it" ON)

if(IDP_STATIC_RUNTIME AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep every intermediate, test, and probe in the build tree. Only
# cmake --install populates the clean, copyable bin/ release tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/shared-runtime")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/runtime")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/shared-runtime")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../shared")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../shared")
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)

if(UNIX AND NOT APPLE)
    find_program(IDP_PATCHELF_EXECUTABLE patchelf REQUIRED)
    configure_file(packaging/fix-linux-rpaths.cmake.in
                   "${CMAKE_BINARY_DIR}/fix-linux-rpaths.cmake" @ONLY)
endif()

# Set default build type to Debug if none specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (default Debug)" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Enable debug symbols and useful flags in Debug mode
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
    if(MSVC)
        add_compile_options(/W4 /Od /Zi)
    else()
        add_compile_options(-g -O0 -Wall -Wextra -Wno-unused-parameter)
    endif()
endif()

set(PARTOS_ROOT "" CACHE PATH "Optional PartOS checkout used to refresh its layout contract")
option(IDP_REFRESH_PARTOS_LAYOUT
    "Regenerate partos_layout.hpp from PARTOS_ROOT instead of using the release snapshot" OFF)
set(PARTOS_LAYOUT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/src/generated")

if(IDP_REFRESH_PARTOS_LAYOUT)
    if(NOT PARTOS_ROOT OR NOT EXISTS "${PARTOS_ROOT}/Makefile")
        message(FATAL_ERROR
            "IDP_REFRESH_PARTOS_LAYOUT requires -DPARTOS_ROOT=/path/to/partos")
    endif()
    set(PARTOS_LAYOUT_HEADER "${PARTOS_ROOT}/build/partos_layout.hpp")
    file(GLOB_RECURSE PARTOS_LAYOUT_DEPENDS CONFIGURE_DEPENDS
        "${PARTOS_ROOT}/src/*.s" "${PARTOS_ROOT}/src/*.c"
        "${PARTOS_ROOT}/src/*.inc" "${PARTOS_ROOT}/include/*.h"
        "${PARTOS_ROOT}/Makefile" "${PARTOS_ROOT}/tools/check_partos_layout.py")
    add_custom_command(
        OUTPUT "${PARTOS_LAYOUT_HEADER}"
        COMMAND "${CMAKE_MAKE_PROGRAM}" -C "${PARTOS_ROOT}" build/partos_layout.hpp
        DEPENDS ${PARTOS_LAYOUT_DEPENDS}
        COMMENT "Refreshing the PartOS layout contract"
        VERBATIM)
    add_custom_target(partos_layout_header DEPENDS "${PARTOS_LAYOUT_HEADER}")
    set(PARTOS_LAYOUT_INCLUDE_DIR "${PARTOS_ROOT}/build")
else()
    add_custom_target(partos_layout_header)
endif()

function(idp_require_partos_layout target_name)
    add_dependencies(${target_name} partos_layout_header)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PARTOS_LAYOUT_INCLUDE_DIR}
    )
endfunction()

# --- SDL2 (system) ---
find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)

# --- OpenGL ---
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(WIN32)
    find_package(GLEW REQUIRED)
    add_compile_definitions(SDL_MAIN_HANDLED WIN32_LEAN_AND_MEAN NOMINMAX)
    # Winsock is a platform service; vcpkg provides the remaining libraries as
    # static archives for release builds.
    link_libraries(GLEW::GLEW ws2_32)
endif()

# --- Dear ImGui via FetchContent ---
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        fd13a1e8923a0a7077b404fc36fd063b25a0c0b5
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

# --- udap DAP debugger (https://github.com/retro-vault/udap) ---
# Sources are fetched by the Makefile into third_party/udap (gitignored,
# always latest). Only the reusable libraries are built here: libdap
# (DAP protocol) and the SDCC CDB/MAP parsers. udap's own CMake files
# are not used because they assume udap is the top-level project.
set(UDAP_DIR ${CMAKE_SOURCE_DIR}/third_party/udap)
if(NOT EXISTS ${UDAP_DIR}/include/dap/target.h)
    message(FATAL_ERROR
        "udap sources not found at ${UDAP_DIR}.\n"
        "Run 'make fetch' (or 'git clone https://github.com/retro-vault/udap.git ${UDAP_DIR}').")
endif()

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

file(GLOB UDAP_DAP_SOURCES ${UDAP_DIR}/lib/dap/*.cpp)
add_library(udap_dap STATIC ${UDAP_DAP_SOURCES})
target_include_directories(udap_dap
    PUBLIC ${UDAP_DIR}/include
    PRIVATE ${UDAP_DIR}/lib/dap
)
target_link_libraries(udap_dap PUBLIC nlohmann_json::nlohmann_json)

file(GLOB UDAP_SDCC_SOURCES ${UDAP_DIR}/lib/sdcc/*.cpp)
add_library(udap_sdcc STATIC ${UDAP_SDCC_SOURCES})
target_include_directories(udap_sdcc PUBLIC ${UDAP_DIR}/include)

# Subdirectories
add_subdirectory(lib/chipsex)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

configure_file(packaging/release-manifest.txt.in
               "${CMAKE_BINARY_DIR}/release-manifest.txt" @ONLY)
