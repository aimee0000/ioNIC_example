#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

// ================== UART Configuration ==================
#define UART_ID         uart1
#define UART_BAUD       115200
#define UART_TX_PIN     4
#define UART_RX_PIN     5

#define UART_BUF_SIZE   2048

// ---------- IRQ RX Ring Buffer ----------
#define RX_RING_SIZE 4096 
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t rx_ring[RX_RING_SIZE];

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

    printf("=== W55RP20-S2E Loopback Server Demo (UART mode) ===\n");
    {
        while(1)
        {
            while (rx_ring_pop(&c)) {
                uart_putc_raw(UART_ID, c);
                printf("%c", c);
            }
            tight_loop_contents();
        }
    }
    printf("=== W55RP20-S2E Loopback Server Demo (UART mode) ===\n");

    while (true) tight_loop_contents();
   

}
