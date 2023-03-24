cmake_minimum_required(VERSION 3.13)
project(pico_pio_hx711 VERSION 0.0.1 DESCRIPTION "Raspberry Pi Pico HX711 library using PIO")

if (NOT TARGET pico_pio_hx711)
    add_library(pico_pio_hx711 INTERFACE)

    pico_generate_pio_header(pico_pio_hx711 ${CMAKE_CURRENT_LIST_DIR}/hx711.pio)

    target_sources(pico_pio_hx711 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/pio_hx711.cpp
        )

    target_include_directories(pico_pio_hx711 INTERFACE include)

    # not sure if these can be specified in this library or if the executable needs to specify them
    target_link_libraries(pico_pio_hx711 INTERFACE
        pico_stdlib
        hardware_pio
        hardware_dma
        )
endif()