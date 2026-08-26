cmake_minimum_required(VERSION 3.21)

# A release tag is the canonical version source.  Keep an explicit cache value
# available for source archives (which have no .git metadata) and untagged
# developer builds, but never allow it to disagree with an exact release tag.
set(idp_version_fallback "1.1.0")
set(idp_tag_version "")
find_program(IDP_GIT_EXECUTABLE git)
if(IDP_GIT_EXECUTABLE AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${IDP_GIT_EXECUTABLE}" describe --tags --exact-match
                --match "v[0-9]*.[0-9]*.[0-9]*"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE idp_exact_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE idp_exact_tag_result)
    if(idp_exact_tag_result EQUAL 0)
        string(REGEX REPLACE "^v" "" idp_tag_version "${idp_exact_tag}")
        if(NOT idp_tag_version MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
            message(FATAL_ERROR
                "Release tag '${idp_exact_tag}' must have the form v1.2.3")
        endif()
    endif()
endif()

if(NOT DEFINED IDP_VERSION)
    if(idp_tag_version)
        set(IDP_VERSION "${idp_tag_version}")
    else()
        set(IDP_VERSION "${idp_version_fallback}")
    endif()
endif()

if(NOT IDP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "IDP_VERSION '${IDP_VERSION}' must have the form 1.2.3")
endif()
if(idp_tag_version AND NOT IDP_VERSION STREQUAL idp_tag_version)
    message(FATAL_ERROR
        "IDP_VERSION '${IDP_VERSION}' does not match release tag "
        "'v${idp_tag_version}'")
endif()

set(IDP_VERSION "${IDP_VERSION}" CACHE STRING
    "Release version embedded in packages")
project(idp-emu VERSION ${IDP_VERSION} LANGUAGES C CXX)
file(WRITE "${CMAKE_BINARY_DIR}/idp-version.txt" "${PROJECT_VERSION}\n")
if(idp_tag_version)
    message(STATUS "Release version: ${PROJECT_VERSION} (from tag v${idp_tag_version})")
else()
    message(STATUS "Release version: ${PROJECT_VERSION} (untagged build)")
endif()

option(IDP_STATIC_RUNTIME
    "Link the compiler runtime statically where the platform supports it" ON)
option(IDP_BUILD_PROBES
    "Build developer-only floppy and GDP diagnostic probes" OFF)
set(IDP_RETROVAULT_COMMIT
    "c003deb4647e63a7a1db864222f008c0621493c9"
    CACHE STRING "Pinned Retro Vault protocol implementation revision")
mark_as_advanced(IDP_RETROVAULT_COMMIT)

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

# --- SDL2 (system) ---
find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)

# --- OpenGL ---
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    find_program(IDP_CURL_CONFIG_EXECUTABLE curl-config)
    if(IDP_CURL_CONFIG_EXECUTABLE)
        execute_process(
            COMMAND "${IDP_CURL_CONFIG_EXECUTABLE}" --prefix
            OUTPUT_VARIABLE idp_curl_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE idp_curl_prefix_result)
        if(idp_curl_prefix_result EQUAL 0)
            list(PREPEND CMAKE_PREFIX_PATH "${idp_curl_prefix}")
            find_package(CURL QUIET)
        endif()
    endif()
endif()
if(NOT CURL_FOUND)
    message(FATAL_ERROR
        "Internal Retro Vault support requires the libcurl development package")
endif()

# FindCURL may discover a complete non-system runtime (for example Conda)
# while PNG and OpenGL have already been found in the system runtime. Linking
# both sets gives CMake no safe RPATH order: either set can hide ABI-sensitive
# libraries from the other. When curl's runtime contains matching libraries,
# keep the complete dependency set in that one runtime.
if(UNIX AND NOT APPLE)
    set(idp_curl_library "")
    foreach(idp_curl_location_property
            IMPORTED_LOCATION
            IMPORTED_LOCATION_RELEASE
            IMPORTED_LOCATION_DEBUG)
        get_target_property(idp_curl_location CURL::libcurl
                            ${idp_curl_location_property})
        if(idp_curl_location AND EXISTS "${idp_curl_location}")
            set(idp_curl_library "${idp_curl_location}")
            break()
        endif()
    endforeach()

    if(idp_curl_library)
        get_filename_component(idp_curl_library_directory
                               "${idp_curl_library}" DIRECTORY)
        list(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES
             "${idp_curl_library_directory}" idp_curl_implicit_index)
        if(idp_curl_implicit_index EQUAL -1)
            find_file(idp_runtime_zlib
                NAMES libz.so.1
                PATHS "${idp_curl_library_directory}"
                NO_DEFAULT_PATH)
            find_file(idp_runtime_png
                NAMES libpng16.so.16
                PATHS "${idp_curl_library_directory}"
                NO_DEFAULT_PATH)
            find_file(idp_runtime_opengl
                NAMES libOpenGL.so.0
                PATHS "${idp_curl_library_directory}"
                NO_DEFAULT_PATH)
            find_file(idp_runtime_glx
                NAMES libGLX.so.0
                PATHS "${idp_curl_library_directory}"
                NO_DEFAULT_PATH)

            if(idp_runtime_zlib AND idp_runtime_png AND
               idp_runtime_opengl AND idp_runtime_glx)
                foreach(idp_imported_configuration "" _RELEASE _DEBUG)
                    set_property(TARGET ZLIB::ZLIB PROPERTY
                        IMPORTED_LOCATION${idp_imported_configuration}
                        "${idp_runtime_zlib}")
                    set_property(TARGET PNG::PNG PROPERTY
                        IMPORTED_LOCATION${idp_imported_configuration}
                        "${idp_runtime_png}")
                    set_property(TARGET OpenGL::OpenGL PROPERTY
                        IMPORTED_LOCATION${idp_imported_configuration}
                        "${idp_runtime_opengl}")
                    set_property(TARGET OpenGL::GLX PROPERTY
                        IMPORTED_LOCATION${idp_imported_configuration}
                        "${idp_runtime_glx}")
                endforeach()
                message(STATUS
                    "Using curl-compatible runtime libraries from "
                    "${idp_curl_library_directory}")
            endif()
        endif()
    endif()
endif()

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

# Compile only the platform-neutral Retro Vault request/JSON core into the
# emulator. The POSIX squid-server executable, serial transport and plugin
# loader are intentionally not built or shipped.
FetchContent_Declare(
    retrovault_gateway
    GIT_REPOSITORY https://github.com/retro-plastics/squid-server.git
    GIT_TAG        ${IDP_RETROVAULT_COMMIT}
    GIT_SHALLOW    FALSE
)
FetchContent_GetProperties(retrovault_gateway)
if(NOT retrovault_gateway_POPULATED)
    FetchContent_Populate(retrovault_gateway)
endif()
set(IDP_RETROVAULT_SOURCE_DIRECTORY
    "${retrovault_gateway_SOURCE_DIR}/lib/retrovault")

set(IDP_IMGUI_OPENGL3_BACKEND
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
if(WIN32)
    # The application already initializes GLEW after creating its OpenGL
    # context. Reuse that loader in Dear ImGui as well: ImGui's private GL3W
    # loader can fail on otherwise valid fresh Windows installations even
    # after GLEW has loaded the same context successfully.
    set(IDP_IMGUI_OPENGL3_BACKEND
        "${CMAKE_SOURCE_DIR}/src/gui/imgui_impl_opengl3_glew.cpp")
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${IDP_IMGUI_OPENGL3_BACKEND}
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2 OpenGL::GL)
if(WIN32)
    target_link_libraries(imgui_lib PUBLIC GLEW::GLEW)
endif()

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
