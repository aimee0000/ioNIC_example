# loopback_udp_uart
add_executable(loopback_udp_uart
        loopback_udp_uart.c
        )

target_link_libraries(loopback_udp_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(loopback_udp_uart 1)
pico_enable_stdio_uart(loopback_udp_uart 0)

pico_add_extra_outputs(loopback_udp_uart)

# loopback_udp_spi
add_executable(loopback_udp_spi
        loopback_udp_spi.c
        )

target_link_libraries(loopback_udp_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(loopback_udp_spi 1)
pico_enable_stdio_uart(loopback_udp_spi 0)

pico_add_extra_outputs(loopback_udp_spi)