#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/irq.h"

// ================== SPI Configuration ==================
#define SPI_PORT        spi0
#define SPI_BAUD_HZ     (10 * 1000 * 1000)   
#define SPI_TX_PIN      3
#define SPI_RX_PIN      4
#define SPI_CLK_PIN     2
#define SPI_CS_PIN      5
#define SPI_INT_PIN      26

// ================== W55RP20-S2E SPI Protocol ==================
#define CMD_DATA_WRITE   0xA0
#define CMD_DATA_READ    0xB0
#define RESP_READ_HDR    0xB1
#define RESP_ACK         0x0A
#define RESP_NACK        0x0B
#define DUMMY            0xFF

#define TIMEOUT_MS      2000
#define MAX_PAYLOAD      2048   // doc notes >2048 may NACK :contentReference[oaicite:7]{index=7}
#define BUF_SIZE        2048

#define ERR_NACK -1
#define ERR_TIMEOUT -2

static volatile bool spi_rx_pending = false;

void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == SPI_INT_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        spi_rx_pending = true;
    }
}

static int spi_wait_tx_ack_frame(void)
{
    uint8_t tx = DUMMY, rx = 0;

    absolute_time_t start_time = get_absolute_time();
    const int timeout_us = TIMEOUT_MS * 1000;   

    while (absolute_time_diff_us(start_time, get_absolute_time()) < timeout_us)
    {
        spi_read_blocking(SPI_PORT, tx, &rx, 1);
        
        if (rx == RESP_ACK)
        {
            for (int k = 0; k < 3; k++) {
                spi_read_blocking(SPI_PORT, tx, &rx, 1);
            }
            return 0;
        }

        if (rx == RESP_NACK)    return ERR_NACK;

        tight_loop_contents();
    }

    return ERR_TIMEOUT;
}

static int spi_wait_rx_ack_frame(uint16_t *out_len)
{
    uint8_t tx = DUMMY, rx = 0;

    absolute_time_t start_time = get_absolute_time();
    const int timeout_us = TIMEOUT_MS * 1000;   

    while (absolute_time_diff_us(start_time, get_absolute_time()) < timeout_us)
    {
        spi_read_blocking(SPI_PORT, tx, &rx, 1);

        if (rx == RESP_READ_HDR)
        {
            uint8_t len_l = 0, len_h = 0, end_ff = 0;

            spi_read_blocking(SPI_PORT, tx, &len_l, 1);
            spi_read_blocking(SPI_PORT, tx, &len_h, 1);
            spi_read_blocking(SPI_PORT, tx, &end_ff, 1);

            if (out_len) {
                *out_len = (uint16_t)len_l | ((uint16_t)len_h << 8);
            }
            return 0;
        }

        tight_loop_contents();
    }

    return ERR_TIMEOUT;
}

bool spi_write(const uint8_t *data, uint16_t len) {
    int ret = 0;
    if (!data || len == 0) return true;
    if (len > MAX_PAYLOAD) return false; 

    uint8_t hdr[4] = {
        CMD_DATA_WRITE,
        (uint8_t)(len & 0xFF),
        (uint8_t)((len >> 8) & 0xFF),
        DUMMY
    };

    // Step 1: 4-byte command
    spi_write_blocking(SPI_PORT, hdr, 4);

    // Step 2: wait ACK frame
    if((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - spi_write: %d\n", ret);
        return false;
    }

    // Step 3: data
    spi_write_blocking(SPI_PORT, data, len);

    // Step 4: wait ACK frame
    if((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - spi_write: %d\n", ret);
        return false;
    }

    return true;
}

int spi_read(uint8_t *out, uint16_t out_max) {
    int ret = 0;

    if (!out || out_max == 0) return -1;

    // Step 1: send read command
    uint8_t cmd[4] = { CMD_DATA_READ, DUMMY, DUMMY, DUMMY };
    spi_write_blocking(SPI_PORT, cmd, 4);

    // Step 2: wait B1 header frame and get len
    uint16_t len = 0;
    if((ret = spi_wait_rx_ack_frame(&len)) < 0) {
        //printf("Error - spi_read: %d\n", ret);
        return ret;
    }

    if (len == 0) return 0;
    if (len > out_max) return -3;

    // Step 3: data read 
    uint8_t tx = DUMMY;
    spi_read_blocking(SPI_PORT, tx, &out[0], len);

    return (int)len;
}

static bool at_set(const char *at_cmd, const char *val) {
    int ret = 0;
    if (!at_cmd) return false;

    uint16_t cmd_len = strlen(at_cmd);
    if (cmd_len < 2) return false;

    uint16_t val_len = (val ? (uint16_t)strlen(val) : 0);
    uint16_t data_len = cmd_len + val_len;

    uint8_t hdr[4] = {
        (uint8_t)at_cmd[0],
        (uint8_t)at_cmd[1],
        (uint8_t)(data_len & 0xFF),
        (uint8_t)((data_len >> 8) & 0xFF)
    };

    // Step 1: 4-byte command
    spi_write_blocking(SPI_PORT, hdr, 4);

    // Step 2: wait ACK frame
    if((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - at_set: %d\n", ret);
        return false;
    }
    
    // Step 3: set AT command value + CRLF
    if(val != NULL)
        spi_write_blocking(SPI_PORT, (const uint8_t*)&val[0], val_len);
    spi_write_blocking(SPI_PORT, "\r\n", 2);

    // Step 4: wait ACK frame
    if((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - at_set: %d\n", ret);
        return false;
    }
    if (val == NULL)
        printf("AT Set > %.2s\r\n", at_cmd);
    else 
        printf("AT Set > %.2s%s\r\n", at_cmd, val);

    sleep_ms(10);

    return true;
}

static int at_get(const char *at_cmd, char *out) {
    int ret = 0;
    if (!at_cmd || !out) return false;

    uint8_t hdr[4] = {
        (uint8_t)at_cmd[0],
        (uint8_t)at_cmd[1],
        (uint8_t)0x0D,      // '\r'
        (uint8_t)0x0A       // '\n'
    };

    // Step 1: send read command
    spi_write_blocking(SPI_PORT, hdr, 4);

    // Step 2 : Check INT Status
    absolute_time_t start_time = get_absolute_time();
    const int timeout_us = TIMEOUT_MS * 1000;   

    while (absolute_time_diff_us(start_time, get_absolute_time()) < timeout_us)
    {
        if (spi_rx_pending) 
        {
            ret = 0;
            spi_rx_pending = false;
            break;
        }
        tight_loop_contents();
    }
    if(ret < 0) 
    {
        printf("Error - spi_read: %d\n", ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    // Step 3: wait B1 header frame and get len
    uint16_t len = 0;
    if((ret = spi_wait_rx_ack_frame(&len)) < 0) {
        printf("Error - spi_read: %d\n", ret);
        return ret;
    }

    if (len == 0) {
        out[0] = '\0';
        return 0;
    }

    // Step 4: data read 
    uint8_t tx = DUMMY;
    spi_read_blocking(SPI_PORT, tx, &out[0], len);
    out[len]='\0';
    
    printf("AT Get> %s\r\n", out);

    return (int)len;
}

int main() {
    char buf[BUF_SIZE];

    stdio_init_all();
    sleep_ms(3000);

    // SPI init
    spi_init(SPI_PORT, SPI_BAUD_HZ);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CLK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CS_PIN, GPIO_FUNC_SPI);
    bi_decl(bi_4pins_with_func(SPI_RX_PIN, SPI_TX_PIN, SPI_CLK_PIN, SPI_CS_PIN, GPIO_FUNC_SPI));

    gpio_init(SPI_INT_PIN);
    gpio_set_dir(SPI_INT_PIN, GPIO_IN);
    gpio_pull_up(SPI_INT_PIN);

    gpio_set_irq_enabled_with_callback(
        SPI_INT_PIN,
        GPIO_IRQ_EDGE_FALL,
        true,
        &gpio_callback
    );

    printf("=== W55RP20-S2E Loopback Server Demo (SPI mode) ===\n");

    printf("\n--- Config W55RP20 with AT command(SPI) ---\n");
    {
        at_set("FR", NULL);                             // Send Factory Reset command
        printf("W55RP20 is Rebooting...\n"); 
        sleep_ms(3000); 
        at_set("OP", "1");                              // Set W55RP20 TCP server mode
        at_set("LI", "192.168.11.2");                   // Set W55RP20's Local IP : 192.168.11.2
        at_set("SM", "255.255.255.0");                  // Set W55RP20's Subnet mask : 255.255.255.0
        at_set("GW", "192.168.11.1");                   // Set W55RP20's Gateway : 192.168.11.1
        at_set("DS", "8.8.8.8");                        // Set W55RP20's DNS Address : 8.8.8.8
        at_set("LP", "5000");                           // Set W55RP20's Local Port : 5000
        at_set("SV", NULL);                             // Send Save command
        at_set("RT", NULL);                             // Send Reset command
        printf("W55RP20 is Rebooting...\n"); 
        sleep_ms(3000); 
        spi_rx_pending = false;     
    }

    printf("\n--- Loopback Data(SPI) ---\n");
    {
        while (true) {
            if (spi_rx_pending) {
                
                int len = spi_read(buf, sizeof(buf));

                if (len > 0) {
                    // loopback
                    bool ok = spi_write(buf, (uint16_t)len);
                    if(ok == false)
                        printf("TX(%d): FAIL", len);
                }
                spi_rx_pending = false;
            }
            tight_loop_contents();
        }
    }
}
