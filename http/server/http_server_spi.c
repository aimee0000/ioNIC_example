#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/irq.h"

#include "web_page.h"s

// ================== SPI Configuration ==================
#define SPI_PORT        spi0
#define SPI_BAUD_HZ     (10 * 1000 * 1000)
#define SPI_TX_PIN      3
#define SPI_RX_PIN      4
#define SPI_CLK_PIN     2
#define SPI_CS_PIN      5
#define SPI_INT_PIN     26

// ================== W55RP20-S2E SPI Protocol ==================
#define CMD_DATA_WRITE   0xA0
#define CMD_DATA_READ    0xB0
#define RESP_READ_HDR    0xB1
#define RESP_ACK         0x0A
#define RESP_NACK        0x0B
#define DUMMY            0xFF

#define TIMEOUT_MS      5000
#define MAX_PAYLOAD     2048   // doc notes >2048 may NACK :contentReference[oaicite:7]{index=7}
#define BUF_SIZE        2048

#define ERR_NACK        -1
#define ERR_TIMEOUT     -2

// ================== HTTP buffer ==================
#define REQ_BUF_SIZE 2048

static char req_buf[REQ_BUF_SIZE];

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

        if (rx == RESP_NACK) return ERR_NACK;
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
        printf("Error - spi_write command: %d\n", ret);
        return false;
    }

    // Step 3: data
    spi_write_blocking(SPI_PORT, data, len);

    // Step 4: wait ACK frame
    if((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - spi_write data: %d\n", ret);
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

// ================== HTTP Server (SPI) ==================
// Because of the MAX_PAYLOAD limit, the response needs to be sent in chunks
static bool spi_write_str_chunked(const char *s)
{
    if (!s) return false;
    size_t total = strlen(s);
    size_t off = 0;

    while (off < total) {
        size_t chunk = total - off;
        if (chunk > MAX_PAYLOAD) chunk = MAX_PAYLOAD;

        if (!spi_write((const uint8_t*)(s + off), (uint16_t)chunk)) {
            return false;
        }
        off += chunk;

        sleep_ms(1);
    }
    return true;
}

static bool http_parse_path(const char* req, char* out_path, size_t out_sz)
{
    if (!req || !out_path || out_sz == 0) return false;

    const char* line_end = strstr(req, "\r\n");
    if (!line_end) return false;

    char line[256];
    size_t ln = (size_t)(line_end - req);
    if (ln >= sizeof(line)) ln = sizeof(line) - 1;
    memcpy(line, req, ln);
    line[ln] = '\0';

    char method[16] = {0};
    char path[128]  = {0};
    if (sscanf(line, "%15s %127s", method, path) != 2) return false;

    strncpy(out_path, path, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    return true;
}

static uint32_t http_rx_read_request(uint32_t idle_timeout_ms)
{
    uint32_t n = 0;
    char last4[4] = {0,0,0,0};
    absolute_time_t last_rx = get_absolute_time();

    memset(req_buf, 0, sizeof(req_buf));

    while (true) {
        if (spi_rx_pending) {
            spi_rx_pending = false;

            uint8_t buf[BUF_SIZE];
            int len = spi_read(buf, BUF_SIZE);
            if (len > 0) {
                last_rx = get_absolute_time();

                for (int i = 0; i < len; i++) {
                    char c = (char)buf[i];

                    if (n < REQ_BUF_SIZE - 1) {
                        req_buf[n++] = c;
                        req_buf[n] = '\0';
                    }

                    last4[0] = last4[1];
                    last4[1] = last4[2];
                    last4[2] = last4[3];
                    last4[3] = c;

                    if (last4[0] == '\r' && last4[1] == '\n' &&
                        last4[2] == '\r' && last4[3] == '\n') {
                        return n; 
                    }
                }
            }
        }

        // idle timeout
        if (absolute_time_diff_us(last_rx, get_absolute_time()) >
            (int64_t)idle_timeout_ms * 1000) {
            return n; 
        }

        tight_loop_contents();
    }
}

static void http_send_response(const char* path)
{
    const char* body;
    const char* status;

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        body = index_page;
        status = "200 OK";
    }
    else if(strcmp(path, "/favicon.ico") == 0)  {
        body = "";
        status = "200 OK";
    } 
    else {
        body = "<html><body><h1>404 Not Found</h1></body></html>";
        status = "404 Not Found";
    }

    int body_len = (int)strlen(body);

    char hdr[BUF_SIZE];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %s\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, body_len);

    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        printf("HTTP header build failed\n");
        return;
    }

    printf("\n[HTTP TX] %s %s (len=%d)\n", status, path, body_len);

    // 1) header
    if (!spi_write((const uint8_t*)hdr, (uint16_t)hdr_len)) {
        printf("spi_write header failed\n");
        return;
    }

    // 2) body (chunked by MAX_PAYLOAD)
    if (!spi_write_str_chunked(body)) {
        printf("spi_write body failed\n");
        return;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(3000);

    // SPI init
    spi_init(SPI_PORT, SPI_BAUD_HZ);
    gpio_set_function(SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CLK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SPI_CS_PIN, GPIO_FUNC_SPI);
    bi_decl(bi_4pins_with_func(SPI_RX_PIN, SPI_TX_PIN, SPI_CLK_PIN, SPI_CS_PIN, GPIO_FUNC_SPI));

    // INT pin
    gpio_init(SPI_INT_PIN);
    gpio_set_dir(SPI_INT_PIN, GPIO_IN);
    gpio_pull_up(SPI_INT_PIN);

    gpio_set_irq_enabled_with_callback(
        SPI_INT_PIN,
        GPIO_IRQ_EDGE_FALL,
        true,
        &gpio_callback
    );

    printf("=== W55RP20-S2E HTTP Server Demo (SPI mode) ===\n");

    printf("\n--- Config W55RP20 with AT command(SPI) ---\n");
    {
        at_set("FR", NULL);                     // Send Factory Reset command
        printf("W55RP20 is Rebooting...\n"); 
        sleep_ms(3000); 
        at_set("OP", "1");                      // Set W55RP20 TCP server mode
        at_set("LI", "192.168.11.2");           // Set W55RP20's Local IP : 192.168.11.2
        at_set("SM", "255.255.255.0");          // Set W55RP20's Subnet mask : 255.255.255.0
        at_set("GW", "192.168.11.1");           // Set W55RP20's Gateway : 192.168.11.1
        at_set("DS", "8.8.8.8");                // Set W55RP20's DNS Address : 8.8.8.8
        at_set("LP", "50001");                  // Set W55RP20's Local Port(HTTP) : 50001
        at_set("SV", NULL);                     // Send Save command
        sleep_ms(100); 
        at_set("RT", NULL);                     // Send Reset command
        printf("W55RP20 is Rebooting...\n"); 
        sleep_ms(3000); 
        spi_rx_pending = false;     
    }

    sleep_ms(3000); 

    printf("\n--- HTTP Server Running ---\n");
    printf("Open: http://192.168.11.2:50001/  (from PC browser)\n");
    {
        while (true) {
            uint32_t n = http_rx_read_request(15000);

            if (n == 0) {
                tight_loop_contents();
                continue;
            }

            printf("\n[HTTP RX REQUEST] (%u bytes)\n%s\n", n, req_buf);

            char path[128];
            if (!http_parse_path(req_buf, path, sizeof(path))) {
                printf("Request parse failed\n");
                http_send_response("/__parse_failed__");
            } else {
                http_send_response(path);
            }

            spi_rx_pending = false;
            sleep_ms(20);
        }
    }

    printf("=== W55RP20-S2E HTTP Server Demo (SPI mode) ===\n");

    while (true) tight_loop_contents();

}
