add_library(xebec INTERFACE)
target_include_directories(xebec INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chipsex INTERFACE xebec)
