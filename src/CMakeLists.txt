file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp" "*.c")

add_executable(idp-emu ${SOURCES})
target_include_directories(idp-emu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
