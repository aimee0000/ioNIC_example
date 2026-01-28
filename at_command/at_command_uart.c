#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

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

// ---------- IRQ RX Ring Buffer ----------
#define RX_RING_SIZE 4096 
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
static uint8_t rx_ring[RX_RING_SIZE];

static char uart_tx_buf[UART_BUF_SIZE] = {};
static char uart_rx_buf[UART_BUF_SIZE] = {};

static bool commandMode = false;

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
static void printHelp() {
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
    printf("AT Set: %s\r\n", uart_tx_buf);
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
    printf("AT Get: %s\r\n", uart_rx_buf);
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
    commandMode = true;
    sleep_ms(500);
}

void exit_command_mode()
{
    sleep_ms(500);
    at_set("EX", NULL);
    if(!recv_uart_data_line())
        while (true) tight_loop_contents();
    printf("Response > %s\r\n", uart_rx_buf);
    commandMode = false;
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
    commandMode = false;
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
    commandMode = false;
    sleep_ms(3000); 
}

static bool is_exec_wo_cmd(const char code[3])
{
    return (strcmp(code, "SV") == 0) ||
           (strcmp(code, "RT") == 0) ||
           (strcmp(code, "FR") == 0);
}

static bool is_valid_code2(const char code[3])
{
    return ( (isupper((unsigned char)code[0]) || isdigit((unsigned char)code[0])) &&
             (isupper((unsigned char)code[1]) || isdigit((unsigned char)code[1])) );
}

static void handle_input_str(char *buf) 
{
    if(!commandMode && (strncmp(buf, "+++", 3) == 0))
    {
        enter_command_mode();
        return;
    }
    if((strncmp(buf, "HELP", 4) == 0) || (strncmp(buf, "?", 1) == 0))
    {
        printHelp();
        return;
    }
    if(commandMode && (strncmp(buf, "EX", 2) == 0))
    {
        exit_command_mode();
        return;
    }
    if(commandMode && (strncmp(buf, "FR", 2) == 0))
    {
        factory_reset();
        return;
    }
    if(commandMode && (strncmp(buf, "RT", 2) == 0))
    {
        device_reset();
        return;
    }


    if(!commandMode)
    {
        printf("Not in command mode. Type '+++' first.\r\n");
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

    if (!has_arg && !is_exec_wo_cmd(code))
    {
        // GET
        if (!at_get(code)) {
            printf("GET failed: %s\r\n", code);
        }
        return;
    }
    else
    {
        // SET
        const char *val = has_arg ? arg : NULL;
        if (!at_set(code, val)) {
            printf("SET failed: %s\r\n", code);
        }
        if (has_arg) at_get(code);
        return;
    }
        
}

static int usb_readline(char *buf, int buf_size)
{
    int idx = 0;
    bool got_cr = false;

    while (1)
    {
        int ch = getchar_timeout_us(0);  
        if (ch == PICO_ERROR_TIMEOUT)
        {
            tight_loop_contents();
            continue;
        }

        char c = (char)ch;

        // Echo 
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

int main() {
    char str_buf[64];

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

    printf("=== W55RP20-S2E AT Commad Demo(UART) ===\n");
    sleep_ms(500);

    while(1)
    {
        printf("\r\n---------------------------------------------------------------------------\r\n");
        printf("   Ready.\r\n");
        printf("   Type '+++' then Enter to switch to command mode.\r\n");
        printf("   In command mode, type W55RP20 commands (e.g., VR, MN, MC, LI).\r\n");
        printf("   Type 'EX' then Enter to exit command mode.\r\n");
        printf("   Type 'HELP' or '?' for command guide.\r\n");
        printf("---------------------------------------------------------------------------\r\n");

        printf(">> ");
        int len = usb_readline(str_buf, sizeof(str_buf));

        if (len > 0) {
            handle_input_str(str_buf);
        }
    }
    while (true) tight_loop_contents();
}
