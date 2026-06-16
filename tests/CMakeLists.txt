add_executable(test_z80sio
    test_z80sio.cpp
)

target_link_libraries(test_z80sio PRIVATE chipsex)

add_test(NAME z80sio_unit COMMAND test_z80sio)

add_executable(test_terminal_keymap
    test_terminal_keymap.cpp
    ../src/gui/terminal_keymap.cpp
)

target_include_directories(test_terminal_keymap PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_keymap PRIVATE SDL2::SDL2)

add_test(NAME terminal_keymap_unit COMMAND test_terminal_keymap)

add_executable(test_vt52_terminal
    test_vt52_terminal.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_vt52_terminal PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib/chipsex/thomson
    ${CMAKE_SOURCE_DIR}/lib/chipsex/signetics
)
target_compile_definitions(test_vt52_terminal PRIVATE GL_GLEXT_PROTOTYPES)
target_link_libraries(test_vt52_terminal PRIVATE SDL2::SDL2 OpenGL::GL)

add_test(NAME vt52_terminal_unit COMMAND test_vt52_terminal)

add_executable(test_partner_crt_terminal
    test_partner_crt_terminal.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_crt.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partner_crt_terminal PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_crt_terminal PRIVATE GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partner_crt_terminal PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partner_crt_terminal COMMAND test_partner_crt_terminal)

add_executable(test_z80pio
    test_z80pio.cpp
)

target_link_libraries(test_z80pio PRIVATE chipsex)

add_test(NAME z80pio_unit COMMAND test_z80pio)

add_executable(test_partner_gdp_keyboard
    test_partner_gdp_keyboard.cpp
    ../src/partner_gdp_keyboard.cpp
)

target_include_directories(test_partner_gdp_keyboard PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME partner_gdp_keyboard_unit COMMAND test_partner_gdp_keyboard)

add_executable(test_ef9367
    test_ef9367.cpp
)

target_link_libraries(test_ef9367 PRIVATE chipsex)

add_test(NAME ef9367_unit COMMAND test_ef9367)

add_executable(test_scn2674
    test_scn2674.cpp
)

target_include_directories(test_scn2674 PRIVATE ${CMAKE_SOURCE_DIR}/lib/chipsex/signetics)

add_test(NAME scn2674_unit COMMAND test_scn2674)

add_executable(test_disk_controllers
    test_disk_controllers.cpp
)

target_link_libraries(test_disk_controllers PRIVATE chipsex)

add_test(NAME disk_controllers_unit COMMAND test_disk_controllers)

add_executable(test_partos_bios_probe
    test_partos_bios_probe.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_partos_bios_probe PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_bios_probe PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_partos_bios_probe PRIVATE chipsex)

add_test(NAME partos_bios_probe COMMAND test_partos_bios_probe)

add_executable(test_partos_bios_probe_gdp
    test_partos_bios_probe_gdp.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_bios_probe_gdp PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_bios_probe_gdp PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_bios_probe_gdp PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partos_bios_probe_gdp COMMAND test_partos_bios_probe_gdp)

add_executable(test_partos_print
    test_partos_print.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_crt.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_print PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_print PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_print PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partos_print COMMAND test_partos_print)

add_executable(test_partos_boot_banner
    test_partos_boot_banner.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_crt.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_boot_banner PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_boot_banner PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_boot_banner PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partos_boot_banner COMMAND test_partos_boot_banner)

add_executable(test_partos_bios_menu_crt
    test_partos_bios_menu_crt.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_crt.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_bios_menu_crt PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_bios_menu_crt PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_bios_menu_crt PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partos_bios_menu_crt COMMAND test_partos_bios_menu_crt)

add_executable(test_partos_bios_menu_gdp
    test_partos_bios_menu_gdp.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_bios_menu_gdp PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_bios_menu_gdp PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_bios_menu_gdp PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partos_bios_menu_gdp COMMAND test_partos_bios_menu_gdp)

add_executable(test_partos_bios_rtc
    test_partos_bios_rtc.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_partos_bios_rtc PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_bios_rtc PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_partos_bios_rtc PRIVATE chipsex)

add_test(NAME partos_bios_rtc COMMAND test_partos_bios_rtc)

add_executable(test_rtc_nvram_shadow
    test_rtc_nvram_shadow.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_rtc_nvram_shadow PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rtc_nvram_shadow PRIVATE chipsex)

add_test(NAME rtc_nvram_shadow COMMAND test_rtc_nvram_shadow)
