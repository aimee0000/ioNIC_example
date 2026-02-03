#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#include "web_page.h"

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

// ================== HTTP Server Helpers ==================

/**
 * Read HTTP request header until "\r\n\r\n" is seen or idle timeout occurs.
 * - Returns bytes stored in uart_rx_buf (0 means no request)
 */
static uint32_t http_rx_read_request(uint32_t idle_timeout_ms)
{
    uint32_t n = 0;
    absolute_time_t idle_deadline = make_timeout_time_ms(idle_timeout_ms);

    uart_rx_buf[0] = '\0';

    // track tail pattern "\r\n\r\n"
    char last4[4] = {0,0,0,0};

    while (true) {
        uint8_t c;
        if (rx_ring_pop(&c)) {
            idle_deadline = make_timeout_time_ms(idle_timeout_ms);

            // store (bounded)
            if (n < UART_BUF_SIZE - 1) {
                uart_rx_buf[n++] = (char)c;
                uart_rx_buf[n] = '\0';
            }

            // shift last4
            last4[0] = last4[1];
            last4[1] = last4[2];
            last4[2] = last4[3];
            last4[3] = (char)c;

            // header end?
            if (last4[0] == '\r' && last4[1] == '\n' &&
                last4[2] == '\r' && last4[3] == '\n') {
                return n;
            }
        } else {
            if (time_reached(idle_deadline)) {
                // idle timeout: 요청이 완성되지 않았거나, 아무것도 안 들어옴
                return n; // n==0이면 "no request"
            }
            sleep_ms(1);
        }
    }
}

/**
 * Very small HTTP routing:
 * - "/" or "/index.html" -> 200 + HTML
 * - else -> 404
 */
static void http_send_response(const char* path)
{
    const char* body;
    const char* status;

    // ---------- Routing ----------
    if (strcmp(path, "/") == 0 || strcmp(path, "/favicon.ico") == 0) {
        body = index_page;
        status = "200 OK";
    }
    else if(strcmp(path, "/favicon.ico") == 0)  {
        body = "";
        status = "200 OK";
    }
    else {
        body =
            "<html><body><h1>404 Not Found</h1></body></html>";
        status = "404 Not Found";
    }

    int body_len = strlen(body);

    // ---------- Build HTTP Response ----------
    int len = snprintf(uart_tx_buf, sizeof(uart_tx_buf),
                       "HTTP/1.1 %s\r\n"
                       "Content-Type: text/html; charset=UTF-8\r\n"
                       "Content-Length: %d\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       status,
                       body_len,
                       body);

    if (len <= 0 || len >= (int)sizeof(uart_tx_buf)) {
        printf("HTTP response build failed\n");
        return;
    }

    // ---------- Send Response ----------
    uart_puts(UART_ID, uart_tx_buf);

    printf("\n[HTTP RESPONSE SENT]\n");
}


/**
 * Parse request line like: "GET /path HTTP/1.1"
 * Output path into out_path.
 */
static bool http_parse_path(const char* req, char* out_path, size_t out_sz)
{
    if (!req || !out_path || out_sz == 0) return false;

    // find first line end
    const char* line_end = strstr(req, "\r\n");
    if (!line_end) return false;

    // copy first line to temp
    char line[256];
    size_t ln = (size_t)(line_end - req);
    if (ln >= sizeof(line)) ln = sizeof(line) - 1;
    memcpy(line, req, ln);
    line[ln] = '\0';

    // simple parse: METHOD SP PATH SP HTTP/V
    char method[16] = {0};
    char path[128]  = {0};
    if (sscanf(line, "%15s %127s", method, path) != 2) return false;

    // only handle GET for now
    if (strcmp(method, "GET") != 0) {
        strncpy(out_path, "/__unsupported__", out_sz - 1);
        out_path[out_sz - 1] = '\0';
        return true;
    }

    strncpy(out_path, path, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    return true;
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

    printf("=== W55RP20-S2E HTTP Server Demo (UART mode) ===\n");

    printf("\n--- Config W55RP20 with AT command(UART) ---\n");
    {
        enter_command_mode();                   // Send +++ command
        factory_reset();                        // Send Factory Reset command
        // If you send Reset or Factory Reset command, W55RP20 will reboot and enter to default gateway mode
        enter_command_mode();                   // Send +++ command 
        at_set("OP", "1");                      // Set W55RP20 TCP server mode
        at_set("LI", "192.168.11.2");           // Set W55RP20's Local IP : 192.168.11.2
        at_set("SM", "255.255.255.0");          // Set W55RP20's Subnet mask : 255.255.255.0
        at_set("GW", "192.168.11.1");           // Set W55RP20's Gateway : 192.168.11.1
        at_set("DS", "8.8.8.8");                // Set W55RP20's DNS Address : 8.8.8.8
        at_set("LP", "50001");                  // Set W55RP20's Local Port(HTTP) : 50001
        at_set("SV", NULL);                     // Send Save command
        sleep_ms(100);
        device_reset();                         // Send Reset command
        uart_rx_flush();
    }

    printf("\n--- HTTP Server Running ---\n");
    printf("Open: http://192.168.11.2:50001/  (from PC browser)\n");
    {
        while (true) {
            uint32_t n = http_rx_read_request(10000);

            if (n == 0) {
                tight_loop_contents();
                continue;
            }

            printf("\n[HTTP RX REQUEST] (%u bytes)\n%s\n", n, uart_rx_buf);

            char path[128];
            if (!http_parse_path(uart_rx_buf, path, sizeof(path))) {
                printf("Request parse failed\n");
                http_send_response("/__parse_failed__");
            } else {
                http_send_response(path);
            }

            uart_rx_flush();
            sleep_ms(50);
        }
    }


    printf("=== W55RP20-S2E HTTP Server Demo (UART mode) ===\n");

    while (true) tight_loop_contents();
}
