add_executable(test_z80sio
    test_z80sio.cpp
)

target_link_libraries(test_z80sio PRIVATE chipsex)

add_test(NAME z80sio_unit COMMAND test_z80sio)

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
