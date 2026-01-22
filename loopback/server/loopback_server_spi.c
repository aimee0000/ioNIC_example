#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

// ================== UART 설정 ==================
#define UART_ID         uart0
#define UART_BAUD       115200
#define UART_TX_PIN     0   
#define UART_RX_PIN     1   

#define READ_MS     1000 

#define UART_BUF_SIZE       2048

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

// ================== "전체 커맨드" 정의 테이블 ==================
static const cmd_def_t g_cmds[] = {
    // Device Information (RO)
    {"MC", CMD_RO, NULL},
    {"VR", CMD_RO, NULL},
    {"MN", CMD_RO, NULL},
    {"ST", CMD_RO, NULL},
    {"UN", CMD_RO, NULL},
    {"UI", CMD_RO, NULL},

    // Network Settings (RW)
    {"OP", CMD_RW, "0"},                 // 예: 모드 값은 환경별로 다를 수 있으니 데모값은 최소로(필요시 수정)
    {"IM", CMD_RW, "0"},                 // 0/1 등은 실제 매뉴얼 파라미터에 맞게 수정
    {"LI", CMD_RW, "192.168.11.5"},
    {"SM", CMD_RW, "255.255.255.0"},
    {"GW", CMD_RW, "192.168.11.1"},
    {"DS", CMD_RW, "8.8.8.8"},
    {"LP", CMD_RW, "5000"},
    {"RH", CMD_RW, "192.168.11.10"},
    {"RP", CMD_RW, "5000"},

    // Data UART Settings (RW)
    {"BR", CMD_RW, "115200"},
    {"DB", CMD_RW, "8"},
    {"PR", CMD_RW, "0"},
    {"SB", CMD_RW, "1"},
    {"FL", CMD_RW, "0"},
    {"EC", CMD_RW, "0"},

    // Serial Data Packing (RW)
    {"PT", CMD_RW, "0"},
    {"PS", CMD_RW, "0"},
    {"PD", CMD_RW, "0"},

    // Options (RW)
    {"IT", CMD_RW, "0"},
    {"RI", CMD_RW, "1000"},
    {"CP", CMD_RW, "0"},
    {"NP", CMD_RW, "1234"},
    {"SP", CMD_RW, "HELLO"},
    {"DG", CMD_RW, "0"},
    {"KA", CMD_RW, "0"},
    {"KI", CMD_RW, "10"},
    {"KE", CMD_RW, "10"},
    {"SO", CMD_RW, "5000"},

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
    {"SS", CMD_RW, "2B2B2B"},           // 3-byte hex 예시(환경에 맞게)

    // Device control (WO)
    {"EX", CMD_WO, NULL},
    {"SV", CMD_WO, NULL},
    {"RT", CMD_WO, NULL},
    {"FR", CMD_WO, NULL},

    // Extended commands (Status I/O)
    {"SC", CMD_RW, "0"},
    {"S0", CMD_RO, NULL},
    {"S1", CMD_RO, NULL},
};

static char uart_tx_buf[UART_BUF_SIZE] = {};
static char uart_rx_buf[UART_BUF_SIZE] = {};

uint32_t send_uart_data(const char *s)
{
    uart_puts(UART_ID, s);
    uart_puts(UART_ID, "\r\n");
    printf("Send > %s\r\n", s);
}

uint32_t recv_uart_data()
{
    uint32_t n = 0;
    absolute_time_t end = make_timeout_time_ms(READ_MS);

    while (!time_reached(end) && n < UART_BUF_SIZE - 1) {
        if (uart_is_readable(UART_ID)) {
            uart_rx_buf[n++] = (char)uart_getc(UART_ID);
        } else {
            sleep_ms(1);
        }
    }
    uart_rx_buf[n] = '\0';

    if(n) {
        printf("Recv> %s\r\n", uart_rx_buf);
    }
    else {
        printf("No response\n");
    }

    return n;
}

int main() {
    stdio_init_all();
    sleep_ms(3000); 

    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);

    printf("=== Simple W55RP20-S2E AT Demo ===\n");

    uart_puts(UART_ID, "+++");
    if(!recv_uart_data())
        while (true) tight_loop_contents();


    printf("\n--- STEP1: GET all (RO+RW) ---\n");
    for (size_t i = 0; i < sizeof(g_cmds)/sizeof(g_cmds[0]); i++) {
        if (g_cmds[i].type == CMD_WO) continue;
        send_uart_data(g_cmds[i].code);
        if(!recv_uart_data())
            while (true) tight_loop_contents();
        sleep_ms(30);
    }

    printf("\n--- STEP2: SET demo (RW only) ---\n");
    for (size_t i = 0; i < sizeof(g_cmds)/sizeof(g_cmds[0]); i++) {
        if (g_cmds[i].type != CMD_RW || !g_cmds[i].demo_para) continue;
        // set
        snprintf(uart_tx_buf, sizeof(uart_tx_buf), "%s%s", g_cmds[i].code, g_cmds[i].demo_para);
        send_uart_data(uart_tx_buf);
        sleep_ms(30);

        // get
        send_uart_data(g_cmds[i].code);
        if(!recv_uart_data())
            while (true) tight_loop_contents();
    }

    while (true) tight_loop_contents();
   

}
