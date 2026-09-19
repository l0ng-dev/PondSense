/**
 * @file    k230_uart.c
 * @brief   K230 UART DMA 环形缓冲驱动实现。
 *
 * @details
 * 负责 Receive-to-Idle DMA 接收、环形缓冲读取和短帧发送，
 * 不在中断回调中解析业务协议。
 */

#include "k230_uart.h"
#include <stddef.h>
#include <string.h>

static HAL_StatusTypeDef start_receive(K230Uart *transport)
{
  HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(
      transport->uart,
      transport->dma,
      K230_UART_DMA_SIZE);

  if (status == HAL_OK)
  {
    transport->dma_position = 0U;
    transport->restart_pending = 0U;
    if (transport->uart->hdmarx != NULL)
    {
      __HAL_DMA_DISABLE_IT(transport->uart->hdmarx, DMA_IT_HT);
    }
  }
  else
  {
    transport->restart_pending = 1U;
  }
  return status;
}

/**
 * @brief  初始化 K230 UART Receive-to-Idle DMA 接收。
 * @param  transport K230 UART 传输句柄
 * @param  uart      UART 句柄
 * @retval HAL 状态
 */
HAL_StatusTypeDef K230Uart_Init(K230Uart *transport,
                                UART_HandleTypeDef *uart)
{
  if ((transport == NULL) || (uart == NULL))
  {
    return HAL_ERROR;
  }
  (void)memset(transport, 0, sizeof(*transport));
  transport->uart = uart;
  return start_receive(transport);
}

/**
 * @brief  检查 DMA 接收状态并准备下一次接收。
 * @param  transport K230 UART 传输句柄
 * @retval 0 正常；1 已恢复接收；2 接收错误
 */
uint8_t K230Uart_Process(K230Uart *transport)
{
  if ((transport == NULL) || (transport->restart_pending == 0U))
  {
    return 0U;
  }
  (void)HAL_UART_AbortReceive(transport->uart);
  return (uint8_t)((start_receive(transport) == HAL_OK) ? 1U : 2U);
}

/**
 * @brief  发送一帧 K230 协议文本。
 * @param  transport K230 UART 传输句柄
 * @param  data      数据地址
 * @param  length    数据长度
 * @retval HAL 状态
 */
HAL_StatusTypeDef K230Uart_Send(K230Uart *transport,
                               const uint8_t *data,
                               uint16_t length)
{
  if ((transport == NULL) || (transport->uart == NULL) ||
      (data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }
  return HAL_UART_Transmit(transport->uart, (uint8_t *)data, length, 20U);
}

/**
 * @brief  从接收环形缓冲区读取一个字节。
 * @param  transport K230 UART 传输句柄
 * @param  value     输出字节地址
 * @retval 1 成功读取；0 缓冲区为空或参数无效
 */
uint8_t K230Uart_ReadByte(K230Uart *transport, uint8_t *value)
{
  uint16_t tail;

  if ((transport == NULL) || (value == NULL) ||
      (transport->tail == transport->head))
  {
    return 0U;
  }
  tail = transport->tail;
  *value = transport->ring[tail];
  transport->tail = (uint16_t)((tail + 1U) % K230_UART_RING_SIZE);
  return 1U;
}

static void push_byte(K230Uart *transport, uint8_t value)
{
  uint16_t next = (uint16_t)((transport->head + 1U) % K230_UART_RING_SIZE);
  if (next == transport->tail)
  {
    transport->overflow_count++;
    return;
  }
  transport->ring[transport->head] = value;
  transport->head = next;
}

void K230Uart_RxEventCallback(K230Uart *transport,
                              UART_HandleTypeDef *uart,
                              uint16_t size)
{
  uint16_t index;
  uint16_t position;

  if ((transport == NULL) || (uart != transport->uart))
  {
    return;
  }
  if (size > K230_UART_DMA_SIZE)
  {
    size = K230_UART_DMA_SIZE;
  }
  position = transport->dma_position;
  if (size >= position)
  {
    for (index = position; index < size; index++)
    {
      push_byte(transport, transport->dma[index]);
    }
  }
  else
  {
    for (index = position; index < K230_UART_DMA_SIZE; index++)
    {
      push_byte(transport, transport->dma[index]);
    }
    for (index = 0U; index < size; index++)
    {
      push_byte(transport, transport->dma[index]);
    }
  }
  transport->dma_position = (size == K230_UART_DMA_SIZE) ? 0U : size;
}

void K230Uart_ErrorCallback(K230Uart *transport,
                            UART_HandleTypeDef *uart)
{
  if ((transport == NULL) || (uart != transport->uart))
  {
    return;
  }
  transport->error_count++;
  transport->restart_pending = 1U;
}
