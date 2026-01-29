# W55RP20-S2E C/C++ Example
This project provides example codes for controlling the W55RP20-S2E (Serial-to-Ethernet) module using a Raspberry Pi Pico.

The Pico transmits data and commands via UART or SPI, allowing the W55RP20-S2E to be configured through AT commands or to run network applications.

For more details about W55RP20-S2E, please refer to the official documentation:

https://docs.wiznet.io/Product/Chip/MCU/Pre-programmed-MCU/W55RP20-S2E/overview-en


## Example List
| Interface   | File       | Description     | 
|---------------|---------------|----------------------------------------|
|  UART  | at_command/at_command_uart.c |  Example for configuring W55RP20-S2E by sending commands entered through USB serial from Pico via UART  | 
|  UART  | loopback/loopback_server_uart.c |  Configures the W55RP20-S2E as a TCP server via UART and relays UART data from the module back to it transparently  |
|  UART  | loopback/loopback_client_uart.c |  Configures the W55RP20-S2E as a TCP client via UART and relays UART data from the module back to it transparently  |
|  UART  | loopback/loopback_udp_uart.c |  Configures the W55RP20-S2E as a UDP via UART and relays UART data from the module back to it transparently |
|  UART  | (To be added...) |  (To be added...)  |
|  SPI  | at_command/at_command_spi.c |  Example for configuring W55RP20-S2E by sending commands entered through USB serial from Pico via SPI  | 
|  SPI  | loopback/loopback_server_spi.c | Configures the W55RP20-S2E as a TCP server via SPI and relays SPI data from the module back to it transparently  |
|  SPI  | loopback/loopback_client_spi.c |  Configures the W55RP20-S2E as a TCP client via SPI and relays SPI data from the module back to it transparently  |
|  SPI  | loopback/loopback_udp_spi.c |  Configures the W55RP20-S2E as a UDP via SPI and relays SPI data from the module back to it transparently  |
|  SPI  | (To be added...) |  (To be added...)  |

## Hardware Setup
### W55RP20 Interface Selection
| Pin   | State  |  Desc  | 
|------------|------------|------------|
| GPIO13 | LOW(GND) | UART mode(default) |
| GPIO13 | HIGH(3.3V) | SPI mode |

### UART example
| Pico   | W55RP20  |  Desc  | 
|------------|------------|------------|
| GPIO4(UART_TX) | GPIO5(UART_RX) | |
| GPIO5(UART_RX) | GPIO4(UART_TX) | |
| GND | GND | |


### SPI example
| Pico   | W55RP20  |  Desc  | 
|------------|------------|------------|
| GPIO2(SPI_CLK) | GPIO2(SPI_CLK) | CLK |
| GPIO3(SPI_TX) | GPIO4(SPI_RX) | MOSI |
| GPIO4(SPI_RX) | GPIO3(SPI_TX) | MISO |
| GPIO5(SPI_CS) | GPIO5(SPI_CS) | CS |
| GPIO26(SPI_INT) | GPIO26(SPI_INT) | INT |
| GND | GND | |
