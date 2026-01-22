#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

// ================== UART Configuration ==================
#define UART_ID         uart1
#define UART_BAUD       115200
#define UART_TX_PIN     4
#define UART_RX_PIN     5

#define READ_MS         5000
#define UART_BUF_SIZE   2048

typedef enum {
    CMD_RO,
    CMD_RW,
    CMD_WO
} cmd_type_t;

typedef struct {
    const char *code;      
    cmd_type_t type;
    const char *demo_para;  
} cmd_def_t;

// ================== "All Commands" Definition Table ==================
static const cmd_def_t g_cmds[] = {
    // Device Information (RO)
    {"MC", CMD_RO, NULL},
    {"VR", CMD_RO, NULL},
    {"MN", CMD_RO, NULL},
    {"ST", CMD_RO, NULL},
    {"UN", CMD_RO, NULL},
    {"UI", CMD_RO, NULL},

    // Network Settings (RW)
    {"OP", CMD_RW, "1"},                 
    {"IM", CMD_RW, "0"},                 
    {"LI", CMD_RW, "192.168.11.102"},
    {"SM", CMD_RW, "255.255.255.0"},
    {"GW", CMD_RW, "192.168.11.1"},
    {"DS", CMD_RW, "8.8.8.8"},
    {"LP", CMD_RW, "5000"},
    {"RH", CMD_RW, "192.168.11.3"},
    {"RP", CMD_RW, "5000"},

    // Data UART Settings (RW)
    {"BR", CMD_RW, "12"},
    {"DB", CMD_RW, "1"},
    {"PR", CMD_RW, "0"},
    {"SB", CMD_RW, "0"},
    {"FL", CMD_RW, "0"},
    {"EC", CMD_RW, "0"},

    // Serial Data Packing (RW)
    {"PT", CMD_RW, "0"},
    {"PS", CMD_RW, "0"},
    {"PD", CMD_RW, "00"},

    // Options (RW)
    {"IT", CMD_RW, "0"},
    {"RI", CMD_RW, "3000"},
    {"CP", CMD_RW, "0"},
    //{"NP", CMD_RW, "1234"},
    //{"SP", CMD_RW, "HELLO"},
    {"DG", CMD_RW, "1"},
    {"KA", CMD_RW, "1"},
    {"KI", CMD_RW, "7000"},
    {"KE", CMD_RW, "5000"},
    {"SO", CMD_RW, "2000"},

    // Modbus (RW)
    {"PO", CMD_RW, "0"},

    // MQTT (RW)
    {"QU", CMD_RW, "user"},
    {"QP", CMD_RW, "pass"},
    {"QC", CMD_RW, "clientid"},
    {"QK", CMD_RW, "60"},
    {"PU", CMD_RW, "topic/pub"},
    {"U0", CMD_RW, "topic/sub0"},
    {"U1", CMD_RW, "topic/sub1"},
    {"U2", CMD_RW, "topic/sub2"},
    {"QO", CMD_RW, "0"},

    // Certificate
    {"RC", CMD_RW, "0"},
    {"CE", CMD_RW, "0"},
    {"OC", CMD_WO, NULL},
    {"LC", CMD_WO, NULL},
    {"PK", CMD_WO, NULL},

    // Command mode switch settings
    {"TE", CMD_RW, "1"},
    //{"SS", CMD_RW, "2B2B2B"},           

    // Device control (WO)
    {"EX", CMD_WO, NULL},
    {"SV", CMD_WO, NULL},
    {"RT", CMD_WO, NULL},
    {"FR", CMD_WO, NULL},

    // Extended commands (Status I/O)
    {"S0", CMD_RO, NULL},
    {"S1", CMD_RO, NULL},
};

// ---------- IRQ RX Ring Buffer ----------
#define RX_RING_SIZE 4096 
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t rx_ring[RX_RING_SIZE];

static char uart_tx_buf[UART_BUF_SIZE] = {};
static char uart_rx_buf[UART_BUF_SIZE] = {};

// ring buffer helpers
static inline bool rx_ring_is_empty(void) {
    return rx_head == rx_tail;
}
static inline bool rx_ring_is_full(void) {
    return (uint16_t)(rx_head + 1) % RX_RING_SIZE == rx_tail;
}
static inline void rx_ring_push(uint8_t c) {
    uint16_t next = (uint16_t)(rx_head + 1) % RX_RING_SIZE;
    if (next == rx_tail) {
        rx_tail = (uint16_t)(rx_tail + 1) % RX_RING_SIZE;
    }
    rx_ring[rx_head] = c;
    rx_head = next;
}
static inline bool rx_ring_pop(uint8_t *out) {
    if (rx_ring_is_empty()) return false;
    *out = rx_ring[rx_tail];
    rx_tail = (uint16_t)(rx_tail + 1) % RX_RING_SIZE;
    return true;
}

// UART RX IRQ handler
static void on_uart_rx(void) {
    while (uart_is_readable(UART_ID)) {
        uint8_t c = (uint8_t)uart_getc(UART_ID);
        rx_ring_push(c);
    }
}

static void uart_rx_flush(void)
{
    uint8_t c;
    while (rx_ring_pop(&c)) {;}
}

/**
 * Collect data from the IRQ ring buffer into uart_rx_buf.
 * - Default: stop when CRLF ("\r\n") is received (single-line response)
 * - Or stop by READ_MS timeout
 */
uint32_t recv_uart_data_line(void)
{
    uint32_t n = 0;
    bool got_cr = false;
    absolute_time_t end = make_timeout_time_ms(READ_MS);

    uart_rx_buf[0] = '\0';

    while (!time_reached(end) && n < UART_BUF_SIZE - 1) {
        uint8_t c;
        if (rx_ring_pop(&c)) {
            uart_rx_buf[n++] = (char)c;

            if (got_cr && c == '\n') {
                break;
            }
            got_cr = (c == '\r');
        } else {
            sleep_ms(1);
        }
    }

    uart_rx_buf[n] = '\0';
    return n;
}

// AT command helpers
bool at_set(const char* cmd, const char* val)
{
    if (cmd == NULL) return false;

    uart_rx_flush();
    if(val == NULL)
        snprintf(uart_tx_buf, sizeof(uart_tx_buf), "%s", cmd);
    else
        snprintf(uart_tx_buf, sizeof(uart_tx_buf), "%s%s", cmd, val);

    uart_puts(UART_ID, uart_tx_buf);
    uart_puts(UART_ID, "\r\n");
    printf("AT Set > %s\r\n", uart_tx_buf);
    sleep_ms(100);
    return true;
}

bool at_get(const char* cmd)
{
    if (cmd == NULL) return false;

    uart_rx_flush();              
    uart_puts(UART_ID, cmd);
    uart_puts(UART_ID, "\r\n");
    if (!recv_uart_data_line()) {
        printf("No response\n");
        return false;             // timeout
    }
    printf("AT Get> %s\r\n", uart_rx_buf);
    sleep_ms(100);

    return true;
}

void enter_command_mode()
{
    uart_rx_flush();
    uart_puts(UART_ID, "+++");
    if(!recv_uart_data_line())
        while (true) tight_loop_contents();
    printf("Response > %s\r\n", uart_rx_buf);
    sleep_ms(500);
}

void exit_command_mode()
{
    sleep_ms(500);
    at_set("SV", NULL);
    sleep_ms(500);
    at_set("EX", NULL);
    if(!recv_uart_data_line())
        while (true) tight_loop_contents();
    printf("Response > %s\r\n", uart_rx_buf);
    sleep_ms(500);
}

int main() {
    stdio_init_all();
    sleep_ms(3000);

    // ================== UART Init ==================
    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UART_ID, false, false);

    uart_set_irq_enables(UART_ID, true, false);
    irq_set_exclusive_handler(UART1_IRQ, on_uart_rx);
    irq_set_enabled(UART1_IRQ, true);

    printf("=== W55RP20-S2E AT Commad Demo ===\n");
    sleep_ms(500);

    printf("\n--- STEP1: Enter Command Mode ---\n");
    {
        enter_command_mode();
    }
  
    printf("\n--- STEP2: GET all (RO+RW) ---\n");
    {
        for (size_t i = 0; i < sizeof(g_cmds)/sizeof(g_cmds[0]); i++) {
            if (g_cmds[i].type == CMD_WO) continue;

            if( !(at_get(g_cmds[i].code)) ) {
                while (true) tight_loop_contents();
            }
        }
    }

    printf("\n--- STEP3: SET demo (RW only) ---\n");
    {
        at_set("OP", "0");
        if( !(at_get("OP")) ) {
            while (true) tight_loop_contents();
        }

        at_set("LI", "192.168.11.2");
        if( !(at_get("LI")) ) {
            while (true) tight_loop_contents();
        }

        at_set("SM", "255.255.255.0");
        if( !(at_get("SM")) ) {
            while (true) tight_loop_contents();
        }

        at_set("GW", "192.168.11.1");
        if( !(at_get("GW")) ) {
            while (true) tight_loop_contents();
        }

        at_set("DS", "8.8.8.8");
        if( !(at_get("DS")) ) {
            while (true) tight_loop_contents();
        }

        at_set("LP", "5000");
        if( !(at_get("LP")) ) {
            while (true) tight_loop_contents();
        }

        at_set("RH", "192.168.11.100");
        if( !(at_get("RH")) ) {
            while (true) tight_loop_contents();
        }

        at_set("RP", "5000");
        if( !(at_get("RP")) ) {
            while (true) tight_loop_contents();
        }
    }

    printf("\n--- STEP4: Save Settings & Exit Command Mode ---\n");
    {
        exit_command_mode();
    }

    printf("=== W55RP20-S2E AT Commad Demo ===\n");

    while (true) tight_loop_contents();
}
