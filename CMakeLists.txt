cmake_minimum_required(VERSION 3.16)
project(idp-emu LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
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

set(PARTOS_LAYOUT_HEADER ${CMAKE_SOURCE_DIR}/partos/build/partos_layout.hpp)
file(GLOB_RECURSE PARTOS_LAYOUT_DEPENDS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/partos/src/*.s
    ${CMAKE_SOURCE_DIR}/partos/src/*.c
    ${CMAKE_SOURCE_DIR}/partos/src/*.inc
    ${CMAKE_SOURCE_DIR}/partos/include/*.h
    ${CMAKE_SOURCE_DIR}/partos/Makefile
    ${CMAKE_SOURCE_DIR}/tools/check_partos_layout.py
)
add_custom_command(
    OUTPUT ${PARTOS_LAYOUT_HEADER}
    COMMAND ${CMAKE_MAKE_PROGRAM} -C ${CMAKE_SOURCE_DIR}/partos build/partos_layout.hpp
    DEPENDS ${PARTOS_LAYOUT_DEPENDS}
    COMMENT "Generating PartOS layout header"
    VERBATIM
)
add_custom_target(partos_layout_header DEPENDS ${PARTOS_LAYOUT_HEADER})

function(idp_require_partos_layout target_name)
    add_dependencies(${target_name} partos_layout_header)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/partos/build
    )
endfunction()

# --- SDL2 (system) ---
find_package(SDL2 REQUIRED)

# --- OpenGL ---
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

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
