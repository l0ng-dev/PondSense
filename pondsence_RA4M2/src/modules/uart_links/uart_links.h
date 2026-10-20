#ifndef UART_LINKS_H
#define UART_LINKS_H

#include <stdint.h>

#include "bsp_api.h"
#include "../../protocol/k230/k230.h"

/* 回调更新丢字节与故障计数，供主循环检测链路错误。 */
typedef struct
{
    volatile uint32_t open_result;
    volatile uint32_t ring_drops;
    volatile uint32_t rx_faults;
} uart_esp_rx_status_t;

typedef struct
{
    volatile uint32_t open_result;
    volatile uint32_t ring_drops;
    volatile uint32_t rx_faults;
    volatile uint32_t line_overflows;
} uart_k230_rx_status_t;

extern uart_esp_rx_status_t g_uart_esp_rx;
extern uart_k230_rx_status_t g_uart_k230_rx;

void Uart_Links_Init(void);
void Uart_Links_Process(void);
typedef void (*uart_esp_byte_handler_t)(uint8_t value);
void Uart_Links_RegisterEspHandler(uart_esp_byte_handler_t handler);
fsp_err_t Uart_Links_EspWrite(uint8_t const * data, uint16_t length);
typedef void (*uart_k230_frame_handler_t)(PondSense_K230_FrameTypeDefData const * frame);
void Uart_Links_RegisterK230Handler(uart_k230_frame_handler_t handler);
fsp_err_t Uart_Links_K230SendObserve(uint32_t event_id);

#endif
