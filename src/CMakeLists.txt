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
    gui/gui.cpp
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
target_link_libraries(idp-emu PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)

add_executable(idp-probe probe_floppy.cpp ${IDP_CORE_SOURCES})
target_link_libraries(idp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)

add_executable(idp-gdp-probe probe_gdp.cpp ${IDP_CORE_SOURCES})
target_link_libraries(idp-gdp-probe PRIVATE chipsex imgui_lib SDL2::SDL2 OpenGL::GL)

# Optional: define your own debug macro if needed
target_compile_definitions(idp-emu PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
target_compile_definitions(idp-probe PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
target_compile_definitions(idp-gdp-probe PRIVATE GL_GLEXT_PROTOTYPES $<$<CONFIG:Debug>:IDP_DEBUG>)
