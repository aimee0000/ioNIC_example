# W55RP20-S2E C/C++ Example
Raspberry Pi Pico에서 W55RP20-S2E(Serial-to-Ethernet)로 UART/SPI 데이터를 전송해, W55RP20-S2E(Serial-to-Ethernet)를 제어하거나 네트워크 동작을 실행하는 예제 프로젝트입니다.

W55RRP20-S2E 관련 자료는 해당 링크 참고 부탁드립니다.

https://docs.wiznet.io/Product/Chip/MCU/Pre-programmed-MCU/W55RP20-S2E/overview-en


## 예제 구성
| Interface   | File       | Description     | 
|---------------|---------------|----------------------------------------|
|  UART  | at_command/at_command_uart.c |  Raspberry Pi Pico에서 USB로 입력받은 command를 Serial로 내보내 W55RP20-S2E를 설정하는 예제  | 
|  UART  | loopback/loopback_server_uart.c |  W55RP20-S2E를 TCP server 모드로 설정 후 Serial로 수신한 데이터를 그대로 송신하는 예제  |
|  UART  | loopback/loopback_client_uart.c |  W55RP20-S2E를 TCP client 모드로 설정 후 Serial로 수신한 데이터를 그대로 송신하는 예제  |
|  UART  | loopback/loopback_udp_uart.c |  W55RP20-S2E를 UDP 모드로 설정 후 Serial로 수신한 데이터를 그대로 송신하는 예제  |
|  UART  | ...추가예정... |  ...추가예정...  |
|  SPI  | at_command/at_command_spi.c |  Raspberry Pi Pico에서 USB로 입력받은 command를 SPI로 내보내 W55RP20-S2E를 설정하는 예제  | 
|  SPI  | loopback/loopback_server_spi.c |  W55RP20-S2E를 TCP server 모드로 설정 후 SPI로 수신한 데이터를 그대로 송신하는 예제  |
|  SPI  | loopback/loopback_client_spi.c |  W55RP20-S2E를 TCP client 모드로 설정 후 SPI로 수신한 데이터를 그대로 송신하는 예제  |
|  SPI  | loopback/loopback_udp_spi.c |  W55RP20-S2E를 UDP 모드로 설정 후 SPI로 수신한 데이터를 그대로 송신하는 예제  |
|  SPI  | ...추가예정... |  ...추가예정...  |

## 하드웨어 구성
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
