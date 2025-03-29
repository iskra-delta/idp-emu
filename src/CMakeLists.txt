file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.cpp)

add_executable(idp-emu ${SOURCES})

# Link chip headers
target_link_libraries(idp-emu PRIVATE chipsex)
