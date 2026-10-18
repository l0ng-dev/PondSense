#ifndef UART_RX_H
#define UART_RX_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_api.h"
#include "../../protocol/k230/k230.h"

#define UART_RX_ESP_LINE_CAPACITY 128U

/* 回调写接收计数，主循环写解析结果；供 Keil Watch 分别查看。 */
typedef struct
{
    volatile uint32_t open_result;
    volatile uint32_t rx_bytes;
    volatile uint32_t ring_drops;
    volatile uint32_t rx_faults;
    volatile uint32_t parity_errors;
    volatile uint32_t framing_errors;
    volatile uint32_t hardware_overflows;
    volatile uint32_t break_events;
    volatile uint32_t line_count;
    volatile uint32_t line_overflows;
    char last_line[UART_RX_ESP_LINE_CAPACITY];
} uart_esp_rx_status_t;

typedef struct
{
    volatile uint32_t open_result;
    volatile uint32_t rx_bytes;
    volatile uint32_t ring_drops;
    volatile uint32_t rx_faults;
    volatile uint32_t parity_errors;
    volatile uint32_t framing_errors;
    volatile uint32_t hardware_overflows;
    volatile uint32_t break_events;
    volatile uint32_t valid_frames;
    volatile uint32_t invalid_frames;
    volatile uint32_t line_overflows;
    volatile uint32_t last_parse_status;
    PondSense_K230_FrameTypeDefData last_frame;
} uart_k230_rx_status_t;

extern uart_esp_rx_status_t g_uart_esp_rx;
extern uart_k230_rx_status_t g_uart_k230_rx;

/* Watch 中写 request：1 发一次 ESP AT，2 请求应用发起下一编号的 K230 OBS。
 * 主循环消费请求；发送缓冲区保持有效直到 TX_COMPLETE。 */
typedef struct
{
    volatile uint32_t request;
    volatile uint32_t esp_attempts;
    volatile uint32_t esp_write_result;
    volatile uint32_t esp_tx_completed;
    volatile uint32_t k230_attempts;
    volatile uint32_t k230_write_result;
    volatile uint32_t k230_tx_completed;
} uart_probe_status_t;

extern uart_probe_status_t g_uart_probe;

void Uart_Rx_Init(void);
void Uart_Rx_Process(void);
typedef void (*uart_esp_byte_handler_t)(uint8_t value);
void Uart_Rx_RegisterEspHandler(uart_esp_byte_handler_t handler);
fsp_err_t Uart_Rx_EspWrite(uint8_t const * data, uint16_t length);
bool Uart_Rx_EspTxBusy(void);
typedef void (*uart_k230_frame_handler_t)(PondSense_K230_FrameTypeDefData const * frame);
void Uart_Rx_RegisterK230Handler(uart_k230_frame_handler_t handler);
fsp_err_t Uart_Rx_K230SendObserve(uint32_t event_id);

#endif
