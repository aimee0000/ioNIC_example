# at_command_uart
add_executable(at_command_uart
        at_command_uart.c
        )

target_link_libraries(at_command_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(at_command_uart 1)
pico_enable_stdio_uart(at_command_uart 0)

pico_add_extra_outputs(at_command_uart)

# at_command_spi
add_executable(at_command_spi
        at_command_spi.c
        )

target_link_libraries(at_command_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(at_command_spi 1)
pico_enable_stdio_uart(at_command_spi 0)

pico_add_extra_outputs(at_command_spi)