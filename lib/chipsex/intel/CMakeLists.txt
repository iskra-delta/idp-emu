# zilog is header-only, nothing to compile
add_library(intel INTERFACE)
target_include_directories(intel INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# link to main chipsex
target_link_libraries(chipsex INTERFACE intel)
