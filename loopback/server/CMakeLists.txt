# loopback_server_uart
add_executable(loopback_server_uart
        loopback_server_uart.c
        )

target_link_libraries(loopback_server_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(loopback_server_uart 1)
pico_enable_stdio_uart(loopback_server_uart 0)

pico_add_extra_outputs(loopback_server_uart)

# loopback_server_spi
add_executable(loopback_server_spi
        loopback_server_spi.c
        )

target_link_libraries(loopback_server_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(loopback_server_spi 1)
pico_enable_stdio_uart(loopback_server_spi 0)

pico_add_extra_outputs(loopback_server_spi)