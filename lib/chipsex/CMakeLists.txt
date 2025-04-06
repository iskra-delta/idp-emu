add_library(chipsex INTERFACE)

# Add vendor subdirs
add_subdirectory(zilog)
add_subdirectory(intel)
add_subdirectory(national)
add_subdirectory(signetics)

# You can optionally gather all include paths here
target_include_directories(chipsex INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/zilog
    ${CMAKE_CURRENT_SOURCE_DIR}/intel
    ${CMAKE_CURRENT_SOURCE_DIR}/national
    ${CMAKE_CURRENT_SOURCE_DIR}/signetics
)