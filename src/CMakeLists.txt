file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.cpp)

add_executable(idp-emu ${SOURCES})

target_link_libraries(idp-emu PRIVATE chipsex)

# Optional: define your own debug macro if needed
target_compile_definitions(idp-emu PRIVATE $<$<CONFIG:Debug>:IDP_DEBUG>)
