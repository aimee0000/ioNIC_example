# aws_mqtts_uart
add_executable(aws_mqtts_uart
        aws_mqtts_uart.c
        )

target_link_libraries(aws_mqtts_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(aws_mqtts_uart 1)
pico_enable_stdio_uart(aws_mqtts_uart 0)

pico_add_extra_outputs(aws_mqtts_uart)

# aws_mqtts_spi
add_executable(aws_mqtts_spi
        aws_mqtts_spi.c
        )

target_link_libraries(aws_mqtts_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(aws_mqtts_spi 1)
pico_enable_stdio_uart(aws_mqtts_spi 0)

pico_add_extra_outputs(aws_mqtts_spi)