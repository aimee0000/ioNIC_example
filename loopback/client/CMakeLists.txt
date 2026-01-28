# loopback_client_uart
add_executable(loopback_client_uart
        loopback_client_uart.c
        )

target_link_libraries(loopback_client_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(loopback_client_uart 1)
pico_enable_stdio_uart(loopback_client_uart 0)

pico_add_extra_outputs(loopback_client_uart)

# loopback_client_spi
add_executable(loopback_client_spi
        loopback_client_spi.c
        )

target_link_libraries(loopback_client_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(loopback_client_spi 1)
pico_enable_stdio_uart(loopback_client_spi 0)

pico_add_extra_outputs(loopback_client_spi)