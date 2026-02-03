#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
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
#define SPI_INT_PIN     26

// ================== W55RP20-S2E SPI Protocol ==================
#define CMD_DATA_WRITE   0xA0
#define CMD_DATA_READ    0xB0
#define RESP_READ_HDR    0xB1
#define RESP_ACK         0x0A
#define RESP_NACK        0x0B
#define DUMMY            0xFF

#define TIMEOUT_MS       2000
#define MAX_PAYLOAD      2048   // doc notes >2048 may NACK

#define ERR_NACK         -1
#define ERR_TIMEOUT      -2

static volatile bool spi_rx_pending = false;

// ================== Console Configuration ==================
#define CONSOLE_BUF      64
#define AT_RX_BUF_SIZE   2048

static char at_rx_buf[AT_RX_BUF_SIZE];

static void gpio_callback(uint gpio, uint32_t events) {
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

static bool spi_write(const uint8_t *data, uint16_t len)
{
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
    if ((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - spi_write: %d\r\n", ret);
        return false;
    }

    // Step 3: data
    spi_write_blocking(SPI_PORT, data, len);

    // Step 4: wait ACK frame
    if ((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - spi_write: %d\r\n", ret);
        return false;
    }

    return true;
}

static int spi_read(uint8_t *out, uint16_t out_max)
{
    int ret = 0;

    if (!out || out_max == 0) return -1;

    // Step 1: send read command
    uint8_t cmd[4] = { CMD_DATA_READ, DUMMY, DUMMY, DUMMY };
    spi_write_blocking(SPI_PORT, cmd, 4);

    // Step 2: wait B1 header frame and get len
    uint16_t len = 0;
    if ((ret = spi_wait_rx_ack_frame(&len)) < 0) {
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

static bool at_set(const char *at_cmd, const char *val)
{
    int ret = 0;
    if (!at_cmd) return false;

    uint16_t cmd_len = (uint16_t)strlen(at_cmd);
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
    if ((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - at_set: %d\r\n", ret);
        return false;
    }

    // Step 3: set AT command value + CRLF
    if (val != NULL) 
        spi_write_blocking(SPI_PORT, (const uint8_t *)&val[0], val_len);
    spi_write_blocking(SPI_PORT, "\r\n", 2);

    // Step 4: wait ACK frame
    if ((ret = spi_wait_tx_ack_frame()) < 0) {
        printf("Error - at_set: %d\r\n", ret);
        return false;
    }

    if (val == NULL)
        printf("AT Set > %.2s\r\n", at_cmd);
    else 
        printf("AT Set > %.2s%s\r\n", at_cmd, val);
    
    sleep_ms(10);

    return true;
}

static int at_get(const char *at_cmd, char *out)
{
    int ret = 0;
    if (!at_cmd || !out) return -1;

    uint8_t hdr[4] = {
        (uint8_t)at_cmd[0],
        (uint8_t)at_cmd[1],
        0x0D,      // '\r'
        0x0A       // '\n'
    };

    // Step 1: send read command
    spi_write_blocking(SPI_PORT, hdr, 4);

    // Step 2 : Check INT Status
    absolute_time_t start_time = get_absolute_time();
    const int timeout_us = TIMEOUT_MS * 1000;

    while (absolute_time_diff_us(start_time, get_absolute_time()) < timeout_us)
    {
        if (spi_rx_pending) {
            spi_rx_pending = false;
            ret = 0;
            break;
        }
        tight_loop_contents();
    }
    if (ret < 0) {
        printf("Error - at_get_spi: %d\r\n", ERR_TIMEOUT);
        return ERR_TIMEOUT;
    }

    // Step 3: wait B1 header frame and get len
    uint16_t len = 0;
    if ((ret = spi_wait_rx_ack_frame(&len)) < 0) {
        printf("Error - at_get_spi: %d\r\n", ret);
        return ret;
    }

    if (len == 0) {
        out[0] = '\0';
        return 0;
    }

    // Step 4: data read
    uint8_t tx = DUMMY;
    spi_read_blocking(SPI_PORT, tx, (uint8_t *)&out[0], len);
    out[len] = '\0';

    printf("AT Get > %s\r\n", out);
    return (int)len;
}

// AT command helpers
static void printHelp(void)
{
  printf("<<< W55RP20-S2E AT Help >>>\r\n");
  printf("Enter command mode: +++ (guard time >= 500ms before/after)\r\n");
  printf("Exit command mode: EX\r\n");
  printf("Save settings: SV  | Reboot: RT  | Factory reset: FR\r\n");
  printf("\r\n");
  printf("[Device Info] (RO)\r\n");
  printf("MC  -> MAC address (ex: MC00:08:DC:00:00:01)\r\n");
  printf("VR  -> Firmware version (ex: VR1.0.0)\r\n");
  printf("MN  -> Product name (ex: MNWIZ5XXRSR-RP)\r\n");
  printf("ST  -> Status (BOOT/OPEN/CONNECT/UPGRADE/ATMODE)\r\n");
  printf("UN  -> UART interface str (ex: UNRS-232/TTL)\r\n");
  printf("UI  -> UART interface code (ex: UI0)\r\n");
  printf("\r\n");
  printf("[Network] (RW)\r\n");
  printf("OPx -> Mode: 0 TCP client, 1 TCP server, 2 mixed, 3 UDP, 4 SSL, 5 MQTT, 6 MQTTS\r\n");
  printf("IMx -> IP alloc: 0 static, 1 DHCP\r\n");
  printf("LIa.b.c.d -> Local IP (ex: LI192.168.11.2)\r\n");
  printf("SMa.b.c.d -> Subnet (ex: SM255.255.255.0)\r\n");
  printf("GWa.b.c.d -> Gateway (ex: GW192.168.11.1)\r\n");
  printf("DSa.b.c.d -> DNS (ex: DS8.8.8.8)\r\n");
  printf("LPn -> Local port (ex: LP5000)\r\n");
  printf("RHa.b.c.d / domain -> Remote host (ex: RH192.168.11.3)\r\n");
  printf("RPn -> Remote port (ex: RP5000)\r\n");
  printf("\r\n");
  printf("[UART] (RW)\r\n");
  printf("BRx -> Baud (12=115200, 13=230400)\r\n");
  printf("DBx -> Data bits (0=7bit, 1=8bit)\r\n");
  printf("PRx -> Parity (0=None, 1=Odd, 2=Even)\r\n");
  printf("SBx -> Stop bits (0=1bit, 1=2bit)\r\n");
  printf("FLx -> Flow (0=None, 1=XON/XOFF, 2=RTS/CTS)\r\n");
  printf("ECx -> Echo (0=Off, 1=On)\r\n");
  printf("\r\n");
  printf("[Packing] (RW)\r\n");
  printf("PTn -> Time delimiter ms (ex: PT1000)\r\n");
  printf("PSn -> Size delimiter bytes (ex: PS64)\r\n");
  printf("PDxx -> Char delimiter hex (ex: PD0D)\r\n");
  printf("\r\n");
  printf("[Options] (RW)\r\n");
  printf("ITn -> Inactivity sec (ex: IT30)\r\n");
  printf("RIn -> Reconnect interval ms (ex: RI3000)\r\n");
  printf("CPx -> Conn password enable (0/1)\r\n");
  printf("NPxxxx -> Conn password (max 8 chars)\r\n");
  printf("SPxxxx -> Search ID (max 8 chars)\r\n");
  printf("DGx -> Debug msg (0/1)\r\n");
  printf("KAx -> Keep-alive (0/1)\r\n");
  printf("KIn -> KA initial interval ms (ex: KI7000)\r\n");
  printf("KEn -> KA retry interval ms (ex: KE5000)\r\n");
  printf("SOn -> SSL recv timeout ms (ex: SO2000)\r\n");
  printf("\r\n");
  printf("[MQTT] (RW)\r\n");
  printf("QUuser QPpass QCid QK60 PUtopic\r\n");
  printf("U0sub U1sub U2sub QO0\r\n");
  printf("\r\n");
  printf("Type HELP or ? to show this list again.\r\n");
}

static bool is_exec_wo_cmd(const char code[3])
{
    return (strcmp(code, "SV") == 0) ||
           (strcmp(code, "RT") == 0) ||
           (strcmp(code, "FR") == 0);
}

static bool is_valid_code2(const char code[3])
{
    return ((isupper((unsigned char)code[0]) || isdigit((unsigned char)code[0])) &&
            (isupper((unsigned char)code[1]) || isdigit((unsigned char)code[1])));
}

static int usb_readline(char *buf, int buf_size)
{
    int idx = 0;
    bool got_cr = false;

    while (1)
    {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        char c = (char)ch;

        // echo
        putchar(c);

        if (c == '\r') {
            got_cr = true;
            continue;
        }
        if (got_cr && c == '\n') {
            break;
        }
        got_cr = false;

        if (idx < buf_size - 1) {
            buf[idx++] = c;
        }
    }

    buf[idx] = '\0';
    return idx;
}

static void handle_input_str(char *buf)
{
    if (!buf) return;

    if ((strncmp(buf, "HELP", 4) == 0) || (strncmp(buf, "?", 1) == 0)) {
        printHelp();
        return;
    }

    size_t len = strlen(buf);
    if (len < 2) {
        printf("Invalid input. Need at least 2 chars.\r\n");
        return;
    }

    char code[3] = {0};
    code[0] = buf[0];
    code[1] = buf[1];

    if (!is_valid_code2(code)) {
        printf("Invalid command code: %c%c\r\n", code[0], code[1]);
        return;
    }

    const char *arg = buf + 2;
    bool has_arg = (arg[0] != '\0');

    if (!has_arg) {
        if (is_exec_wo_cmd(code)) {
            // EXEC (no argument)
            if (!at_set(code, NULL)) {
                printf("EXEC failed: %s\r\n", code);
            }
            return;
        } else {
            // GET
            int r = at_get(code, at_rx_buf);
            if (r < 0) {
                printf("GET failed (%s): %d\r\n", code, r);
            }
            return;
        }
    } else {
        // SET
        if (!at_set(code, arg)) {
            printf("SET failed: %s\r\n", code);
            return;
        }
        // If it's a normal RW item, read back (same behavior as at_command_uart.c)
        if (!is_exec_wo_cmd(code)) {
            int r = at_get(code, at_rx_buf);
            if (r < 0) {
                printf("GET-after-SET failed (%s): %d\r\n", code, r);
            }
        }
        return;
    }
}

int main(void)
{
    char line[CONSOLE_BUF];

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

    printf("=== W55RP20-S2E AT Command Console (SPI mode) ===\r\n");
    sleep_ms(500);

    while (1)
    {
        printf("\r\n---------------------------------------------------------------------------\r\n");
        printf("   Ready.\r\n");
        printf("   In command mode, type W55RP20 commands (e.g., VR, MN, MC, LI).\r\n");
        printf("   Type 'HELP' or '?' for command guide.\r\n");
        printf("---------------------------------------------------------------------------\r\n");

        printf(">> ");
        int n = usb_readline(line, sizeof(line));
        if (n > 0) {
            handle_input_str(line);
        }
        tight_loop_contents();
    }
    while (true) tight_loop_contents();
}
