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
idp_require_partos_layout(test_partner_crt_terminal)

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

add_executable(test_partner_gdp_memacc
    test_partner_gdp_memacc.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partner_gdp_memacc PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_gdp_memacc PRIVATE GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partner_gdp_memacc PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partner_gdp_memacc)

add_test(NAME partner_gdp_memacc_unit COMMAND test_partner_gdp_memacc)

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
idp_require_partos_layout(test_partos_boot_banner)

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
idp_require_partos_layout(test_partos_bios_menu_crt)

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
idp_require_partos_layout(test_partos_bios_menu_gdp)

add_test(NAME partos_bios_menu_gdp COMMAND test_partos_bios_menu_gdp)

add_executable(test_partos_gdp_ls_scroll
    test_partos_gdp_ls_scroll.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_gdp_ls_scroll PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_gdp_ls_scroll PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_gdp_ls_scroll PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_gdp_ls_scroll)

add_test(NAME partos_gdp_ls_scroll COMMAND test_partos_gdp_ls_scroll)

add_executable(test_partos_gdp_shell_edit
    test_partos_gdp_shell_edit.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_gdp_shell_edit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_gdp_shell_edit PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_gdp_shell_edit PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_gdp_shell_edit)

add_test(NAME partos_gdp_shell_edit COMMAND test_partos_gdp_shell_edit)

add_executable(test_partos_mkdir_return
    test_partos_mkdir_return.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_mkdir_return PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_mkdir_return PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_mkdir_return PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_mkdir_return)

add_test(NAME partos_mkdir_return COMMAND test_partos_mkdir_return)

add_executable(test_partos_cd_return
    test_partos_cd_return.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_cd_return PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_cd_return PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_cd_return PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_cd_return)

add_test(NAME partos_cd_return COMMAND test_partos_cd_return)

add_executable(test_partos_cp_return
    test_partos_cp_return.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_cp_return PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_cp_return PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_cp_return PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_cp_return)

add_test(NAME partos_cp_return COMMAND test_partos_cp_return)

add_executable(test_partos_cwd_command_lookup
    test_partos_cwd_command_lookup.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_cwd_command_lookup PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_cwd_command_lookup PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_cwd_command_lookup PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_cwd_command_lookup)

add_test(NAME partos_cwd_command_lookup COMMAND test_partos_cwd_command_lookup)

add_executable(test_partos_mv_cross_dir_lookup
    test_partos_mv_cross_dir_lookup.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_mv_cross_dir_lookup PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_mv_cross_dir_lookup PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_mv_cross_dir_lookup PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_mv_cross_dir_lookup)

add_test(NAME partos_mv_cross_dir_lookup COMMAND test_partos_mv_cross_dir_lookup)

add_executable(test_partos_cd_fd0_missing
    test_partos_cd_fd0_missing.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partos_cd_fd0_missing PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partos_cd_fd0_missing PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}" GL_GLEXT_PROTOTYPES)
target_link_libraries(test_partos_cd_fd0_missing PRIVATE chipsex SDL2::SDL2 OpenGL::GL)
idp_require_partos_layout(test_partos_cd_fd0_missing)

add_test(NAME partos_cd_fd0_missing COMMAND test_partos_cd_fd0_missing)

add_executable(test_rtc_nvram_shadow
    test_rtc_nvram_shadow.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_rtc_nvram_shadow PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_rtc_nvram_shadow PRIVATE chipsex)
idp_require_partos_layout(test_rtc_nvram_shadow)

add_test(NAME rtc_nvram_shadow COMMAND test_rtc_nvram_shadow)

# Hermetic PartOS kernel boot-floor regression gate (target defined in src/).
# Builds the kernel + a FAT12 fixture and verifies the kernel boots through
# device probe, the round-robin scheduler and FAT mount-submit.
add_test(NAME partos_kernel_boot COMMAND idp-kernel-probe)

# ROM loader byte-for-byte verification for the real HD split image path.
add_test(NAME partos_bootload COMMAND idp-bootload-probe partos/bin/partos.rom hd)

# ROM -> HDD kernel load -> scheduler -> FAT shell read (no kernel overlay).
add_test(NAME partos_full_boot COMMAND idp-full-boot-probe)

# Match the normal F5 launch topology: mounted FD0 first, then fall through to
# the HD boot path that carries the split PartOS image.
add_test(NAME partos_full_boot_fd0 COMMAND idp-full-boot-probe)
set_tests_properties(partos_full_boot_fd0 PROPERTIES
    ENVIRONMENT "IDP_BOOT_VIA_FD0=1"
)

# End-to-end shell regression pack: boots to the prompt and drives a scripted
# sequence of real keypress commands (ls/echo/mkdir/cd/cp/mv/run/rename/del/
# rmdir + cwd-only-lookup and slash-path negative cases), asserting output and
# detecting freezes/timeouts.
add_test(NAME partos_shell_regression COMMAND idp-shell-regression)

set_tests_properties(
    partos_kernel_boot
    partos_bootload
    partos_full_boot
    partos_full_boot_fd0
    partos_shell_regression
    PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

# Freeze the emulated RTC to a fixed epoch so PartOS boots are reproducible.
# The mm58167 otherwise latches host time() on every access (see
# lib/chipsex/national/mm58167.h), which makes the screen-scraping golden tests
# flap PASS/FAIL run-to-run even though the build is byte-for-byte deterministic.
# APPEND so tests that already set ENVIRONMENT (e.g. full_boot_fd0) keep theirs.
foreach(idp_partos_test
    partos_boot_banner partos_bios_menu_crt partos_bios_menu_gdp
    partos_gdp_ls_scroll partos_gdp_shell_edit partos_mkdir_return
    partos_cd_return partos_cp_return partos_cwd_command_lookup
    partos_mv_cross_dir_lookup partos_cd_fd0_missing rtc_nvram_shadow
    partos_kernel_boot partos_bootload partos_full_boot
    partos_full_boot_fd0 partos_shell_regression)
  set_property(TEST ${idp_partos_test} APPEND PROPERTY
      ENVIRONMENT "IDP_FIXED_RTC=1700000000")
endforeach()
