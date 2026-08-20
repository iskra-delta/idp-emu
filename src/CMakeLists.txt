set(IDP_CORE_SOURCES
    chips_impl.cpp
    partner.cpp
    partner_crt.cpp
    partner_gdp.cpp
    partner_gdp_keyboard.cpp
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
idp_require_partos_layout(idp-emu)

add_executable(idp-probe probe_floppy.cpp ${IDP_CORE_SOURCES})
target_link_libraries(idp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)
target_compile_definitions(idp-probe PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
idp_require_partos_layout(idp-probe)

add_executable(idp-gdp-probe probe_gdp.cpp ${IDP_CORE_SOURCES})
target_link_libraries(idp-gdp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)
target_compile_definitions(idp-gdp-probe PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
idp_require_partos_layout(idp-gdp-probe)

add_executable(idp-mcp
    mcp_main.cpp
    mcp/idp_mcp_server.cpp
    ${IDP_CORE_SOURCES}
)
target_link_libraries(idp-mcp PRIVATE
    chipsex SDL2::SDL2 PNG::PNG OpenGL::GL nlohmann_json::nlohmann_json)
target_compile_definitions(idp-mcp PRIVATE
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    GL_GLEXT_PROTOTYPES
    $<$<CONFIG:Debug>:IDP_DEBUG>
)
idp_require_partos_layout(idp-mcp)

# Optional: define your own debug macro if needed
target_compile_definitions(idp-emu PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
target_compile_definitions(idp-probe PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
target_compile_definitions(idp-gdp-probe PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
