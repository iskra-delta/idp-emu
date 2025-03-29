# zilog is header-only, nothing to compile
add_library(thompson INTERFACE)
target_include_directories(thompson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# link to main chipsex
target_link_libraries(chipsex INTERFACE thompson)
