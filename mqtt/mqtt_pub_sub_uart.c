#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

// ================== UART Configuration ==================
#define UART_ID         uart1
#define UART_BAUD       115200
#define UART_TX_PIN     4
#define UART_RX_PIN     5

#define READ_MS         5000
#define UART_BUF_SIZE   2048

// ---------- IRQ RX Ring Buffer ----------
#define RX_RING_SIZE 4096 
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t rx_ring[RX_RING_SIZE];

static char uart_tx_buf[UART_BUF_SIZE] = {};
static char uart_rx_buf[UART_BUF_SIZE] = {};

// ======= Server Information =======
const char* SERVER_IP   = "192.168.11.100";     // PC(Server) IP
const char* SERVER_PORT = "1883";               // PC(Server) MQTT Port 
const char* MQTT_CLIENT_ID = "user";            // MQTT client id
const char* PUB_TOPIC = "/w55rp20/pub";         // MQTT Publish Topic
const char* SUB_TOPIC = "/w55rp20/sub";         // MQTT Subsribe Topic

typedef enum _MODE{
    MODE_NONE,
    MODE_AT,
    MODE_GW,
    MODE_MAX
} MODE;

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

// AT command helper
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
    int ret = recv_uart_data_line();
    if (ret < 0) {
        printf("Timeout waiting response.\r\n");
        return;
    }
    printf("Response > %s\r\n", uart_rx_buf);
    sleep_ms(500);
}

void exit_command_mode() 
{ 
    sleep_ms(500); 
    at_set("EX", NULL); 
    if(!recv_uart_data_line()) 
        while (true) tight_loop_contents(); 
    printf("Response > %s\r\n", uart_rx_buf); 
    sleep_ms(500); 
}

void factory_reset() 
{ 
    at_set("FR", NULL); 
    sleep_ms(500); 
    if(!recv_uart_data_line()) 
        while (true) tight_loop_contents(); 
    printf("Response > %s\r\n", uart_rx_buf); 
    printf("W55RP20 is Rebooting...\n"); 
    sleep_ms(3000); 
}

void device_reset() 
{ 
    at_set("RT", NULL); 
    sleep_ms(500); 
    if(!recv_uart_data_line()) 
        while (true) tight_loop_contents(); 
    printf("Response > %s\r\n", uart_rx_buf); 
    printf("W55RP20 is Rebooting...\n"); 
    sleep_ms(3000); 
}

int main() {
    uint8_t c;

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

    printf("=== W55RP20-S2E MQTT Client Demo (UART mode) ===\n");

    printf("\n--- Config W55RP20 with AT command(UART) ---\n");
    {
        enter_command_mode();                   // Send +++ command
        factory_reset();                        // Send Factory Reset command
        // If you send Reset or Factory Reset command, W55RP20 will reboot and enter to default gateway mode
        enter_command_mode();                   // Send +++ command 
        at_set("OP", "5");                      // Set W55RP20 MQTT client mode
        at_set("LI", "192.168.11.2");           // Set W55RP20's Local IP : 192.168.11.2
        at_set("SM", "255.255.255.0");          // Set W55RP20's Subnet mask : 255.255.255.0
        at_set("GW", "192.168.11.1");           // Set W55RP20's Gateway : 192.168.11.1
        at_set("DS", "8.8.8.8");                // Set W55RP20's DNS Address : 8.8.8.8
        at_set("LP", "5000");                   // Set W55RP20's Local Port : 5000
        at_set("RH", SERVER_IP);                // Set Remote IP(ex. PC)
        at_set("RP", SERVER_PORT);              // Set Remote Port
        at_set("PT", "10");                     // Set Serial Data Packing Time : 10ms
        at_set("QU", "wiznet");                 // Set MQTT user name
        at_set("QP", "");                       // Set MQTT password
        at_set("QC", MQTT_CLIENT_ID);           // Set MQTT client ID
        at_set("QK", "60");                     // Set MQTT Keep-alive
        at_set("PU", PUB_TOPIC);                // Set MQTT public topic
        at_set("U0", SUB_TOPIC);                // Set MQTT subscribe topic1
        at_set("Q0", "0");                      // Set MQTT QoS level
        at_set("SV", NULL);                     // Send Save command
        sleep_ms(100);
        device_reset();                         // Send Reset command
        uart_rx_flush();
    }

    printf("\n--- MQTT Client Running ---\n");
    snprintf(uart_tx_buf, sizeof(uart_tx_buf), "Hello ioNIC MQTT Test\r\n");

    while (true) {
        uint32_t n = recv_uart_data_line();

        if (n > 0) {
            printf("%s\r\n", uart_rx_buf);
            uart_puts(UART_ID, uart_tx_buf);
        } 

        tight_loop_contents();
    }

    printf("=== W55RP20-S2E MQTT Client Demo (UART mode) ===\n");

    while (true) tight_loop_contents();
}
