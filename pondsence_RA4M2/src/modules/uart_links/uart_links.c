#include "uart_links.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "hal_data.h"

#define UART_RX_RING_CAPACITY 512U
#define UART_RX_PROCESS_BUDGET 64U
#define UART_ESP_TX_CAPACITY 256U

typedef struct
{
    uint8_t data[UART_RX_RING_CAPACITY];
    volatile uint16_t head;
    volatile uint16_t tail;
} uart_rx_ring_t;

static uart_rx_ring_t s_esp_ring;
static uart_rx_ring_t s_k230_ring;
static char s_k230_line[PONDSENSE_K230_FRAME_MAX_LENGTH];
static uint16_t s_k230_length;
static bool s_k230_discard;
static uint32_t s_k230_drops_seen;
static uint32_t s_k230_faults_seen;
static char s_k230_tx[PONDSENSE_K230_FRAME_MAX_LENGTH];
static volatile bool s_k230_tx_busy;
static uart_k230_frame_handler_t s_k230_handler;
static uint8_t s_esp_tx[UART_ESP_TX_CAPACITY];
static volatile bool s_esp_tx_busy;
static uart_esp_byte_handler_t s_esp_handler;
uart_esp_rx_status_t g_uart_esp_rx;
uart_k230_rx_status_t g_uart_k230_rx;

/* 单生产者为 UART ISR，单消费者为主循环；临界区保护完整的出队操作。 */
static bool uart_rx_pop(uart_rx_ring_t * ring, uint8_t * value)
{
    uint32_t const primask = __get_PRIMASK();
    __disable_irq();
    if (ring->head == ring->tail)
    {
        __set_PRIMASK(primask);
        return false;
    }

    *value = ring->data[ring->tail];
    ring->tail = (uint16_t) ((ring->tail + 1U) % UART_RX_RING_CAPACITY);
    __set_PRIMASK(primask);
    return true;
}

static void uart_rx_push(uart_rx_ring_t * ring, uint8_t value, volatile uint32_t * drops)
{
    uint16_t const next = (uint16_t) ((ring->head + 1U) % UART_RX_RING_CAPACITY);
    if (next == ring->tail)
    {
        ++(*drops);
        return;
    }

    ring->data[ring->head] = value;
    ring->head = next;
}

void esp01s_uart_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    switch (p_args->event)
    {
        case UART_EVENT_RX_CHAR:
            uart_rx_push(&s_esp_ring, (uint8_t) p_args->data, &g_uart_esp_rx.ring_drops);
            break;
        case UART_EVENT_ERR_PARITY:
        case UART_EVENT_ERR_FRAMING:
        case UART_EVENT_ERR_OVERFLOW:
        case UART_EVENT_BREAK_DETECT:
            ++g_uart_esp_rx.rx_faults;
            break;
        case UART_EVENT_TX_COMPLETE:
            s_esp_tx_busy = false;
            break;
        default:
            break;
    }
}

void k230_uart_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    switch (p_args->event)
    {
        case UART_EVENT_RX_CHAR:
            uart_rx_push(&s_k230_ring, (uint8_t) p_args->data, &g_uart_k230_rx.ring_drops);
            break;
        case UART_EVENT_ERR_PARITY:
        case UART_EVENT_ERR_FRAMING:
        case UART_EVENT_ERR_OVERFLOW:
        case UART_EVENT_BREAK_DETECT:
            ++g_uart_k230_rx.rx_faults;
            break;
        case UART_EVENT_TX_COMPLETE:
            s_k230_tx_busy = false;
            break;
        default:
            break;
    }
}

void Uart_Links_Init(void)
{
    g_uart_esp_rx.open_result = (uint32_t) R_SCI_UART_Open(&g_uart_esp01s_ctrl, &g_uart_esp01s_cfg);
    g_uart_k230_rx.open_result = (uint32_t) R_SCI_UART_Open(&g_uart_k230_ctrl, &g_uart_k230_cfg);
}

void Uart_Links_RegisterK230Handler(uart_k230_frame_handler_t handler)
{
    /* 只在主循环调用，解析回调也只在主循环执行。 */
    s_k230_handler = handler;
}

void Uart_Links_RegisterEspHandler(uart_esp_byte_handler_t handler)
{
    /* 接收字节在主循环出队后交付，ISR 不运行 AT 或 MQTT 解析。 */
    s_esp_handler = handler;
}

fsp_err_t Uart_Links_EspWrite(uint8_t const * data, uint16_t length)
{
    if ((NULL == data) || (0U == length) || (length > sizeof(s_esp_tx)))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }
    if (s_esp_tx_busy)
    {
        return FSP_ERR_IN_USE;
    }

    /* FSP 异步发送引用缓冲区；完成回调前不可覆盖。 */
    memcpy(s_esp_tx, data, length);
    s_esp_tx_busy = true;
    fsp_err_t const result = R_SCI_UART_Write(&g_uart_esp01s_ctrl, s_esp_tx, length);
    if (FSP_SUCCESS != result)
    {
        s_esp_tx_busy = false;
    }
    return result;
}

fsp_err_t Uart_Links_K230SendObserve(uint32_t event_id)
{
    if (s_k230_tx_busy)
    {
        return FSP_ERR_IN_USE;
    }

    int const length = PondSense_K230_FormatObserve(s_k230_tx, sizeof(s_k230_tx), event_id);
    if (length <= 0)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* FSP 异步发送直接引用静态缓冲区，TX_COMPLETE 后才允许改写。 */
    s_k230_tx_busy = true;
    fsp_err_t const result = R_SCI_UART_Write(&g_uart_k230_ctrl,
                                               (uint8_t const *) s_k230_tx,
                                               (uint32_t) length);
    if (FSP_SUCCESS != result)
    {
        s_k230_tx_busy = false;
    }
    return result;
}

static void uart_k230_process_byte(uint8_t value)
{
    if ('$' == value)
    {
        s_k230_length = 0U;
        s_k230_discard = false;
    }
    if ('\n' == value)
    {
        if (!s_k230_discard && (s_k230_length > 0U))
        {
            PondSense_K230_FrameTypeDefData frame;
            PondSense_K230_ParseStatusTypeDef const result =
                PondSense_K230_ParseFrame(s_k230_line, s_k230_length, &frame);
            if (PONDSENSE_K230_PARSE_OK == result)
            {
                if (NULL != s_k230_handler)
                {
                    s_k230_handler(&frame);
                }
            }
        }
        s_k230_length = 0U;
        s_k230_discard = false;
        return;
    }
    if (s_k230_discard)
    {
        return;
    }
    if (s_k230_length >= (PONDSENSE_K230_FRAME_MAX_LENGTH - 1U))
    {
        ++g_uart_k230_rx.line_overflows;
        s_k230_discard = true;
        return;
    }
    s_k230_line[s_k230_length++] = (char) value;
}

void Uart_Links_Process(void)
{
    uint8_t value;
    for (uint16_t count = 0U;
         (count < UART_RX_PROCESS_BUDGET) && uart_rx_pop(&s_esp_ring, &value);
         ++count)
    {
        if (NULL != s_esp_handler)
        {
            s_esp_handler(value);
        }
    }

    if ((s_k230_drops_seen != g_uart_k230_rx.ring_drops) ||
        (s_k230_faults_seen != g_uart_k230_rx.rx_faults))
    {
        s_k230_drops_seen = g_uart_k230_rx.ring_drops;
        s_k230_faults_seen = g_uart_k230_rx.rx_faults;
        s_k230_length = 0U;
        s_k230_discard = true;
    }
    for (uint16_t count = 0U;
         (count < UART_RX_PROCESS_BUDGET) && uart_rx_pop(&s_k230_ring, &value);
         ++count)
    {
        uart_k230_process_byte(value);
    }
}
