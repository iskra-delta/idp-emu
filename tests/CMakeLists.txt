file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/tests/dump")

add_executable(test_z80sio
    test_z80sio.cpp
)

target_link_libraries(test_z80sio PRIVATE chipsex)

add_test(NAME z80sio_unit COMMAND test_z80sio)

add_executable(test_z80dma test_z80dma.cpp)
target_link_libraries(test_z80dma PRIVATE chipsex)
add_test(NAME z80dma_unit COMMAND test_z80dma)

add_executable(test_z80ctc test_z80ctc.cpp)
target_link_libraries(test_z80ctc PRIVATE chipsex)
add_test(NAME z80ctc_unit COMMAND test_z80ctc)

add_executable(test_squid_link_server test_squid_link_server.cpp)
target_link_libraries(test_squid_link_server PRIVATE idp_internal_squid)
add_test(NAME squid_link_server_unit COMMAND test_squid_link_server)

add_executable(test_retro_vault_core
    "${IDP_RETROVAULT_SOURCE_DIRECTORY}/tests/test_retrovault.c"
    "${IDP_RETROVAULT_SOURCE_DIRECTORY}/retrovault_curl.c")
target_include_directories(test_retro_vault_core PRIVATE
    "${IDP_RETROVAULT_SOURCE_DIRECTORY}"
    "${retrovault_gateway_SOURCE_DIR}/include")
target_link_libraries(test_retro_vault_core PRIVATE
    idp_internal_squid CURL::libcurl)
add_test(NAME retro_vault_core_unit COMMAND test_retro_vault_core)

add_executable(test_udap_util test_udap_util.cpp)
target_link_libraries(test_udap_util PRIVATE udap_sdcc)
add_test(NAME udap_util_unit COMMAND test_udap_util)

add_executable(test_partner_sio
    test_partner_sio.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_partner_sio PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_sio PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_partner_sio PRIVATE chipsex)

add_test(NAME partner_sio_unit COMMAND test_partner_sio)

add_executable(test_terminal_keymap
    test_terminal_keymap.cpp
    ../src/gui/terminal_keymap.cpp
)

target_include_directories(test_terminal_keymap PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_keymap PRIVATE SDL2::SDL2)

add_test(NAME terminal_keymap_unit COMMAND test_terminal_keymap)

add_executable(test_startup_input
    test_startup_input.cpp
    ../src/startup_input.cpp
)

target_include_directories(test_startup_input PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME startup_input_unit COMMAND test_startup_input)

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

add_executable(test_partner_crt_boot
    test_partner_crt_boot.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_crt.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partner_crt_boot PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_crt_boot PRIVATE
    GL_GLEXT_PROTOTYPES
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_partner_crt_boot PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partner_crt_boot_integration COMMAND test_partner_crt_boot)

add_executable(test_partner_crt_hdd_boot
    test_partner_crt_hdd_boot.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_crt.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partner_crt_hdd_boot PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_crt_hdd_boot PRIVATE
    GL_GLEXT_PROTOTYPES
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_partner_crt_hdd_boot PRIVATE
    chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partner_crt_hdd_boot_integration COMMAND test_partner_crt_hdd_boot)
set_tests_properties(partner_crt_hdd_boot_integration PROPERTIES
    # Start five seconds before a minute boundary. With JJ12 incorrectly
    # forced closed this edge diverts CP/M through 0066h before the prompt.
    ENVIRONMENT "IDP_FIXED_RTC=1700000035")

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

add_test(NAME partner_gdp_memacc_unit COMMAND test_partner_gdp_memacc)

add_executable(test_partner_gdp_boot
    test_partner_gdp_boot.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partner_gdp_boot PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_gdp_boot PRIVATE
    GL_GLEXT_PROTOTYPES
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_partner_gdp_boot PRIVATE chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partner_gdp_boot_integration COMMAND test_partner_gdp_boot)

add_executable(test_partner_gdp_hdd_boot
    test_partner_gdp_hdd_boot.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
    ../src/partner_gdp.cpp
    ../src/partner_gdp_keyboard.cpp
    ../src/terminal/terminal_factory.cpp
    ../src/terminal/vt52_terminal.cpp
    ../src/gui/display.cpp
)

target_include_directories(test_partner_gdp_hdd_boot PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_gdp_hdd_boot PRIVATE
    GL_GLEXT_PROTOTYPES
    IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_partner_gdp_hdd_boot PRIVATE
    chipsex SDL2::SDL2 OpenGL::GL)

add_test(NAME partner_gdp_hdd_boot_integration COMMAND test_partner_gdp_hdd_boot)

if(UNIX)
    add_executable(test_partner_paket_flow
        test_partner_paket_flow.cpp
        ../src/chips_impl.cpp
        ../src/partner.cpp
        ../src/partner_crt.cpp
        ../src/partner_gdp.cpp
        ../src/partner_gdp_keyboard.cpp
        ../src/terminal/terminal_factory.cpp
        ../src/terminal/vt52_terminal.cpp
        ../src/gui/display.cpp
    )
    target_include_directories(test_partner_paket_flow PRIVATE
        ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(test_partner_paket_flow PRIVATE
        GL_GLEXT_PROTOTYPES
        IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
    )
    target_link_libraries(test_partner_paket_flow PRIVATE
        chipsex SDL2::SDL2 OpenGL::GL idp_internal_squid)
    add_test(NAME partner_p_paket_flow_integration
        COMMAND test_partner_paket_flow)
    set_tests_properties(partner_p_paket_flow_integration PROPERTIES
        ENVIRONMENT "IDP_TEST_PAKET_MODEL=p")
    add_test(NAME partner_g_paket_flow_integration
        COMMAND test_partner_paket_flow)
    set_tests_properties(partner_g_paket_flow_integration PROPERTIES
        ENVIRONMENT "IDP_TEST_PAKET_MODEL=g")
endif()

add_executable(test_partner_external_im2
    test_partner_external_im2.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_partner_external_im2 PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_external_im2 PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_partner_external_im2 PRIVATE chipsex)

add_test(NAME partner_external_im2_unit COMMAND test_partner_external_im2)

add_executable(test_partner_board
    test_partner_board.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_partner_board PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_board PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_partner_board PRIVATE chipsex)

add_test(NAME partner_board_unit COMMAND test_partner_board)

add_executable(test_partner_covox
    test_partner_covox.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_partner_covox PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_partner_covox PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_partner_covox PRIVATE chipsex)

add_test(NAME partner_covox_unit COMMAND test_partner_covox)

add_executable(test_screen_recorder
    test_screen_recorder.cpp
    ../src/gui/screen_recorder.cpp
)

target_include_directories(test_screen_recorder PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_screen_recorder PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_screen_recorder PRIVATE SDL2::SDL2)

add_test(NAME screen_recorder_unit COMMAND test_screen_recorder)

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

add_executable(test_rtc_nvram_shadow
    test_rtc_nvram_shadow.cpp
    ../src/chips_impl.cpp
    ../src/partner.cpp
)

target_include_directories(test_rtc_nvram_shadow PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_rtc_nvram_shadow PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_rtc_nvram_shadow PRIVATE chipsex)

add_test(NAME rtc_nvram_shadow COMMAND test_rtc_nvram_shadow)

add_executable(test_runtime_paths
    test_runtime_paths.cpp
    ../src/runtime_paths.cpp
)

target_include_directories(test_runtime_paths PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_runtime_paths PRIVATE IDP_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_runtime_paths PRIVATE SDL2::SDL2)

add_test(NAME runtime_paths_unit COMMAND test_runtime_paths)

add_test(NAME mcp_stdio
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_mcp_stdio.py
            $<TARGET_FILE:idp-mcp>)

foreach(partner_test_target
    test_partner_sio
    test_partner_crt_terminal
    test_partner_crt_boot
    test_partner_crt_hdd_boot
    test_partner_gdp_memacc
    test_partner_gdp_boot
    test_partner_gdp_hdd_boot
    test_partner_external_im2
    test_partner_board
    test_partner_covox
    test_rtc_nvram_shadow)
    target_link_libraries(${partner_test_target} PRIVATE idp_internal_squid)
endforeach()
