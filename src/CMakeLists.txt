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
target_link_libraries(idp-emu PRIVATE chipsex imgui_lib SDL2::SDL2 PNG::PNG OpenGL::GL Threads::Threads udap_dap udap_sdcc)
target_compile_definitions(idp-emu PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

if(IDP_BUILD_PROBES)
    add_executable(idp-probe probe_floppy.cpp ${IDP_CORE_SOURCES})
    target_link_libraries(idp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)
    target_compile_definitions(idp-probe PRIVATE
        IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
        GL_GLEXT_PROTOTYPES
        $<$<CONFIG:Debug>:IDP_DEBUG>)

    add_executable(idp-gdp-probe probe_gdp.cpp ${IDP_CORE_SOURCES})
    target_link_libraries(idp-gdp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)
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
target_include_directories(idp-mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(idp-mcp PRIVATE
    chipsex SDL2::SDL2 PNG::PNG OpenGL::GL nlohmann_json::nlohmann_json)
target_compile_definitions(idp-mcp PRIVATE
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    IDP_VERSION="${PROJECT_VERSION}"
    GL_GLEXT_PROTOTYPES
    $<$<CONFIG:Debug>:IDP_DEBUG>
)

install(TARGETS idp-emu idp-mcp
    RUNTIME_DEPENDENCY_SET idp_runtime_dependencies
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin)

if(WIN32)
    install(RUNTIME_DEPENDENCY_SET idp_runtime_dependencies
        DESTINATION shared
        PRE_EXCLUDE_REGEXES
            "^api-ms-.*"
            "^ext-ms-.*"
            "^hvsifiletrust\\.dll$"
            "^wpaxholder\\.dll$"
        POST_EXCLUDE_REGEXES ".*[\\/]System32[\\/].*" ".*[\\/]SysWOW64[\\/].*")
elseif(APPLE)
    install(RUNTIME_DEPENDENCY_SET idp_runtime_dependencies
        DESTINATION shared
        POST_EXCLUDE_REGEXES "^/System/Library/.*" "^/usr/lib/.*")
else()
    install(RUNTIME_DEPENDENCY_SET idp_runtime_dependencies
        DESTINATION shared
        POST_EXCLUDE_REGEXES
            ".*/ld-linux[^/]*\\.so.*"
            ".*/lib(c|m|pthread|dl|rt|resolv|util|gcc_s|stdc\\+\\+)\\.so.*"
            ".*/lib(GL|OpenGL|GLX|EGL|drm|gbm)\\.so.*")
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
install(DIRECTORY "${CMAKE_SOURCE_DIR}/disks/" DESTINATION disks
        FILES_MATCHING PATTERN "*.img")
install(FILES
    "${CMAKE_SOURCE_DIR}/README.md"
    "${CMAKE_SOURCE_DIR}/docs/COMMAND-LINE.md"
    "${CMAKE_SOURCE_DIR}/docs/PACKAGING.md"
    "${CMAKE_BINARY_DIR}/release-manifest.txt"
    DESTINATION docs)
install(FILES "${CMAKE_SOURCE_DIR}/packaging/shared-README.txt"
        DESTINATION shared RENAME README.txt)

# Optional: define your own debug macro if needed
target_compile_definitions(idp-emu PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
