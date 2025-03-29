# zilog is header-only, nothing to compile
add_library(signetics INTERFACE)
target_include_directories(signetics INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# link to main chipsex
target_link_libraries(chipsex INTERFACE signetics)
