/**
 * @file    k230_uart.h
 * @brief   K230 UART DMA 环形缓冲驱动接口。
 *
 * @details
 * 定义 UART DMA 缓冲区、接收回调和非阻塞字节读取接口。
 */

#ifndef K230_UART_H
#define K230_UART_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define K230_UART_DMA_SIZE 128U
#define K230_UART_RING_SIZE 256U

typedef struct
{
  UART_HandleTypeDef *uart;
  uint8_t dma[K230_UART_DMA_SIZE];
  volatile uint16_t dma_position;
  volatile uint8_t ring[K230_UART_RING_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint8_t restart_pending;
  volatile uint32_t overflow_count;
  volatile uint32_t error_count;
} K230Uart;

HAL_StatusTypeDef K230Uart_Init(K230Uart *transport,
                                UART_HandleTypeDef *uart);
uint8_t K230Uart_Process(K230Uart *transport);
HAL_StatusTypeDef K230Uart_Send(K230Uart *transport,
                               const uint8_t *data,
                               uint16_t length);
uint8_t K230Uart_ReadByte(K230Uart *transport, uint8_t *value);
void K230Uart_RxEventCallback(K230Uart *transport,
                              UART_HandleTypeDef *uart,
                              uint16_t size);
void K230Uart_ErrorCallback(K230Uart *transport,
                            UART_HandleTypeDef *uart);

#endif
