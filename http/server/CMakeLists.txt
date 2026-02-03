# http_server_uart
add_executable(http_server_uart
        http_server_uart.c
        )

target_link_libraries(http_server_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(http_server_uart 1)
pico_enable_stdio_uart(http_server_uart 0)

pico_add_extra_outputs(http_server_uart)

# http_server_spi
add_executable(http_server_spi
        http_server_spi.c
        )

target_link_libraries(http_server_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(http_server_spi 1)
pico_enable_stdio_uart(http_server_spi 0)

pico_add_extra_outputs(http_server_spi)