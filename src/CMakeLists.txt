add_library(idp_internal_squid STATIC
    squid_link_server.cpp
    internal_squid_server.cpp
    "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable/state.c"
    "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable/init.c"
    "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable/link.c"
    "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable/ring.c"
    "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable/burst.c"
    "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable/socket.c"
    "${IDP_RETROVAULT_SOURCE_DIRECTORY}/retrovault_core.c"
    "${IDP_RETROVAULT_SOURCE_DIRECTORY}/retrovault_json.c"
)
target_include_directories(idp_internal_squid
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        "${IDP_LIBSQUID_SOURCE_DIRECTORY}/include"
        "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/portable"
        "${IDP_LIBSQUID_SOURCE_DIRECTORY}/lib/squid/contract"
        "${IDP_RETROVAULT_SOURCE_DIRECTORY}"
        "${retrovault_gateway_SOURCE_DIR}/include")
target_compile_definitions(idp_internal_squid PRIVATE SQUID_MAX_SOCKETS=1)
target_link_libraries(idp_internal_squid PUBLIC CURL::libcurl Threads::Threads)
target_compile_features(idp_internal_squid PRIVATE c_std_11 cxx_std_23)

set(IDP_CORE_SOURCES
    chips_impl.cpp
    partner.cpp
    partner_crt.cpp
    partner_gdp.cpp
    partner_gdp_keyboard.cpp
    runtime_paths.cpp
    terminal/terminal_factory.cpp
    terminal/vt52_terminal.cpp
    gui/display.cpp
)

set(IDP_GUI_SOURCES
    main.cpp
    machine_configuration.cpp
    startup_input.cpp
    dap/dap_dbg.cpp
    dap/dap_target.cpp
    dap/dap_debugger.cpp
    gui/gui.cpp
    gui/file_dialog.cpp
    gui/screen_recorder.cpp
    gui/terminal_keymap.cpp
    gui/chrome_style.cpp
    gui/chrome_theme.cpp
    gui/custom_title_bar.cpp
    gui/window_chrome.cpp
    gui/panel_disasm.cpp
    gui/panel_display.cpp
    gui/panel_dma.cpp
    gui/panel_devices.cpp
    gui/panel_monitor.cpp
    gui/panel_ef9367.cpp
    gui/panel_fdc.cpp
    gui/panel_pio.cpp
    gui/panel_regs.cpp
    gui/panel_rtc.cpp
    gui/panel_scn2674.cpp
    gui/panel_sio.cpp
    gui/panel_xebec.cpp
)

add_executable(idp-emu ${IDP_CORE_SOURCES} ${IDP_GUI_SOURCES})
target_link_libraries(idp-emu PRIVATE chipsex imgui_lib SDL2::SDL2 PNG::PNG OpenGL::GL Threads::Threads udap_dap udap_sdcc idp_internal_squid nlohmann_json::nlohmann_json)
target_compile_definitions(idp-emu PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

if(WIN32)
    # Keep the user-facing executable name and icon consistent across the
    # installer, portable archive, Start Menu, and desktop shortcut.
    set(IDP_WINDOWS_ICON
        "${CMAKE_SOURCE_DIR}/assets/icons/partner.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/packaging/windows/partner.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/partner.rc"
        @ONLY)
    set_source_files_properties(
        "${CMAKE_CURRENT_BINARY_DIR}/partner.rc"
        PROPERTIES OBJECT_DEPENDS "${IDP_WINDOWS_ICON}")
    target_sources(idp-emu PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/partner.rc")
    set_target_properties(idp-emu PROPERTIES OUTPUT_NAME partner)
endif()

if(IDP_BUILD_PROBES)
    add_executable(idp-probe probe_floppy.cpp ${IDP_CORE_SOURCES})
    target_link_libraries(idp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL idp_internal_squid)
    target_compile_definitions(idp-probe PRIVATE
        IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        GL_GLEXT_PROTOTYPES
        $<$<CONFIG:Debug>:IDP_DEBUG>)

    add_executable(idp-gdp-probe probe_gdp.cpp ${IDP_CORE_SOURCES})
    target_link_libraries(idp-gdp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL idp_internal_squid)
    target_compile_definitions(idp-gdp-probe PRIVATE
        IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        GL_GLEXT_PROTOTYPES
        $<$<CONFIG:Debug>:IDP_DEBUG>)
endif()

add_executable(idp-mcp
    mcp_main.cpp
    mcp/idp_mcp_server.cpp
    ${IDP_CORE_SOURCES}
)

if(WIN32)
    # Give the headless MCP component its own identity in Explorer, MCP client
    # pickers, and the installed Program Files directory.
    set(IDP_WINDOWS_MCP_ICON
        "${CMAKE_SOURCE_DIR}/assets/icons/mcp.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/packaging/windows/mcp.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mcp.rc"
        @ONLY)
    set_source_files_properties(
        "${CMAKE_CURRENT_BINARY_DIR}/mcp.rc"
        PROPERTIES OBJECT_DEPENDS "${IDP_WINDOWS_MCP_ICON}")
    target_sources(idp-mcp PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/mcp.rc")
endif()

target_include_directories(idp-mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(idp-mcp PRIVATE
    chipsex SDL2::SDL2 PNG::PNG OpenGL::GL nlohmann_json::nlohmann_json
    idp_internal_squid)
target_compile_definitions(idp-mcp PRIVATE
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    IDP_VERSION="${PROJECT_VERSION}"
    GL_GLEXT_PROTOTYPES
    $<$<CONFIG:Debug>:IDP_DEBUG>
)

set(IDP_PROGRAM_INSTALL_DIRECTORY bin)
if(WIN32)
    # Keep both statically linked programs together in Program Files. Windows
    # system DLLs must be resolved from the target machine, never copied from
    # the CI runner beside the executables.
    set(IDP_PROGRAM_INSTALL_DIRECTORY .)
endif()

if(WIN32)
    install(TARGETS idp-emu idp-mcp
        RUNTIME DESTINATION "${IDP_PROGRAM_INSTALL_DIRECTORY}")
elseif(APPLE)
    install(TARGETS idp-emu idp-mcp
        RUNTIME_DEPENDENCY_SET idp_runtime_dependencies
        RUNTIME DESTINATION "${IDP_PROGRAM_INSTALL_DIRECTORY}"
        BUNDLE DESTINATION "${IDP_PROGRAM_INSTALL_DIRECTORY}")
else()
    install(TARGETS idp-emu idp-mcp
        RUNTIME DESTINATION "${IDP_PROGRAM_INSTALL_DIRECTORY}")
endif()

if(APPLE)
    install(RUNTIME_DEPENDENCY_SET idp_runtime_dependencies
        DESTINATION shared
        POST_EXCLUDE_REGEXES "^/System/Library/.*" "^/usr/lib/.*")
elseif(UNIX)
    # The Linux helper resolves conflicts explicitly. This matters on developer
    # machines where a Conda library and the system library can share the same
    # SONAME; install(RUNTIME_DEPENDENCY_SET) treats that as a fatal error.
    install(SCRIPT "${CMAKE_BINARY_DIR}/fix-linux-rpaths.cmake")
endif()

if(IDP_STATIC_RUNTIME AND UNIX AND NOT APPLE
   AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(idp-emu PRIVATE -static-libgcc -static-libstdc++)
    target_link_options(idp-mcp PRIVATE -static-libgcc -static-libstdc++)
endif()

if(UNIX AND NOT APPLE)
    # DT_RPATH is intentional for the portable tree: unlike DT_RUNPATH it is
    # also used for transitive libraries copied into the sibling shared dir.
    target_link_options(idp-emu PRIVATE -Wl,--disable-new-dtags)
    target_link_options(idp-mcp PRIVATE -Wl,--disable-new-dtags)
endif()

install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets" DESTINATION .)
install(FILES
    "${CMAKE_SOURCE_DIR}/roms/partner_crt.rom"
    "${CMAKE_SOURCE_DIR}/roms/partner_gdp.rom"
    DESTINATION roms)
install(FILES "${CMAKE_SOURCE_DIR}/partner_cmos.bin" DESTINATION .)
install(FILES
    "${CMAKE_SOURCE_DIR}/disks/hdd-partner-p-system.img"
    "${CMAKE_SOURCE_DIR}/disks/hdd-partner-g-system.img"
    DESTINATION disks)
if(WIN32)
    install(FILES
        "${CMAKE_SOURCE_DIR}/packaging/windows/partnerp.bat"
        "${CMAKE_SOURCE_DIR}/packaging/windows/partnerg.bat"
        DESTINATION .)
endif()
if(UNIX)
    install(PROGRAMS
        "${CMAKE_SOURCE_DIR}/packaging/linux/partnerp"
        "${CMAKE_SOURCE_DIR}/packaging/linux/partnerg"
        DESTINATION bin)
endif()
install(FILES
    "${CMAKE_SOURCE_DIR}/docs/COMMAND-LINE.md"
    "${CMAKE_SOURCE_DIR}/docs/PACKAGING.md"
    "${CMAKE_BINARY_DIR}/release-manifest.txt"
    DESTINATION docs)
if(NOT WIN32)
    # Keep the runtime layout stable even when every dependency is static.
    # An empty directory is sufficient; do not add a README placeholder.
    install(CODE
        "file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/shared\")")
endif()

# Optional: define your own debug macro if needed
target_compile_definitions(idp-emu PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
