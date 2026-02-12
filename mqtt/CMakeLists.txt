# mqtt_pub_sub_uart
add_executable(mqtt_pub_sub_uart
        mqtt_pub_sub_uart.c
        )

target_link_libraries(mqtt_pub_sub_uart 
        pico_stdlib
        hardware_uart
        hardware_irq
)

pico_enable_stdio_usb(mqtt_pub_sub_uart 1)
pico_enable_stdio_uart(mqtt_pub_sub_uart 0)

pico_add_extra_outputs(mqtt_pub_sub_uart)

# mqtt_pub_sub_spi
add_executable(mqtt_pub_sub_spi
        mqtt_pub_sub_spi.c
        )

target_link_libraries(mqtt_pub_sub_spi 
        pico_stdlib
        hardware_gpio
        hardware_spi
        hardware_irq
)

pico_enable_stdio_usb(mqtt_pub_sub_spi 1)
pico_enable_stdio_uart(mqtt_pub_sub_spi 0)

pico_add_extra_outputs(mqtt_pub_sub_spi)