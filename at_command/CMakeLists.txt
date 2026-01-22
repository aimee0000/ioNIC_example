add_executable(at_command
        at_command.c
        )

target_link_libraries(at_command 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(at_command 1)
pico_enable_stdio_uart(at_command 0)

pico_add_extra_outputs(at_command)