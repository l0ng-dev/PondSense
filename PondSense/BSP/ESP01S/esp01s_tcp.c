/**
 * @file    esp01s_tcp.c
 * @brief   ESP-01S 单路 TCP 非阻塞收发。
 *
 * @details
 * 管理 TCP 建连、发送提示、数据发送、接收缓存和超时失败处理。
 */

#include "esp01s_internal.h"
#include <stdio.h>
#include <string.h>

void BSP_WiFi_ResetTcp(BSP_WiFi_HandleTypeDef *wifi)
{
  wifi->tcp_state = BSP_WIFI_TCP_STATE_CLOSED;
  wifi->tcp_prompt_received = 0U;
  wifi->tcp_ipd_mode = 0U;
  wifi->tcp_ipd_length = 0U;
  wifi->tcp_ipd_received = 0U;
  wifi->tcp_tx_length = 0U;
  wifi->tcp_rx_head = 0U;
  wifi->tcp_rx_tail = 0U;
}


void BSP_WiFi_TcpFailed(BSP_WiFi_HandleTypeDef *wifi,
                        BSP_WiFi_ErrorTypeDef error)
{
  wifi->last_error = error;
  wifi->tcp_state = BSP_WIFI_TCP_STATE_ERROR;
  wifi->tcp_prompt_received = 0U;
}


void BSP_WiFi_PushTcpRxByte(BSP_WiFi_HandleTypeDef *wifi, uint8_t value)
{
  uint16_t next = (uint16_t)((wifi->tcp_rx_head + 1U) % BSP_WIFI_TCP_RX_RING_SIZE);
  if (next == wifi->tcp_rx_tail)
  { wifi->last_error = BSP_WIFI_ERROR_TCP_RX_OVERFLOW; return; }
  wifi->tcp_rx_ring[wifi->tcp_rx_head] = value;
  wifi->tcp_rx_head = next;
}


/**
 * @brief  推进单路 TCP 建连、发送和超时处理。
 * @param  wifi   Wi-Fi 句柄
 * @param  now_ms 当前系统毫秒时间
 */
void BSP_WiFi_ProcessTcp(BSP_WiFi_HandleTypeDef *wifi, uint32_t now_ms)
{
  char command[BSP_WIFI_TCP_HOST_MAX_LENGTH + 48U];
  int written;
  if (wifi->state != BSP_WIFI_STATE_ONLINE)
  { if (wifi->tcp_state != BSP_WIFI_TCP_STATE_CLOSED) BSP_WiFi_ResetTcp(wifi); return; }
  if (((wifi->tcp_state == BSP_WIFI_TCP_STATE_CONNECTING) ||
       (wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_PROMPT) ||
       (wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_SEND_RESULT)) &&
      ((int32_t)(now_ms - wifi->tcp_deadline_ms) >= 0))
  { BSP_WiFi_TcpFailed(wifi, BSP_WIFI_ERROR_TCP_TIMEOUT); return; }
  if (wifi->tcp_state == BSP_WIFI_TCP_STATE_CONNECT_PENDING)
  {
    written = snprintf(command, sizeof(command), "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                       wifi->tcp_host, (unsigned int)wifi->tcp_port);
    if ((written <= 0) || ((size_t)written >= sizeof(command)) ||
        (HAL_UART_Transmit(wifi->uart, (const uint8_t *)command,
                           (uint16_t)written, BSP_WIFI_TX_TIMEOUT_MS) != HAL_OK))
    { BSP_WiFi_TcpFailed(wifi, BSP_WIFI_ERROR_TCP_CONNECT); return; }
    wifi->tcp_state = BSP_WIFI_TCP_STATE_CONNECTING;
    wifi->tcp_deadline_ms = now_ms + BSP_WIFI_TCP_CONNECT_TIMEOUT_MS;
  }
  else if (wifi->tcp_state == BSP_WIFI_TCP_STATE_SEND_PENDING)
  {
    /* CIPSEND 分两步完成：先申报长度，收到 '>' 后再发送原始负载。 */
    written = snprintf(command, sizeof(command), "AT+CIPSEND=%u\r\n",
                       (unsigned int)wifi->tcp_tx_length);
    if ((written <= 0) || ((size_t)written >= sizeof(command)) ||
        (HAL_UART_Transmit(wifi->uart, (const uint8_t *)command,
                           (uint16_t)written, BSP_WIFI_TX_TIMEOUT_MS) != HAL_OK))
    { BSP_WiFi_TcpFailed(wifi, BSP_WIFI_ERROR_TCP_SEND); return; }
    wifi->tcp_prompt_received = 0U;
    wifi->tcp_state = BSP_WIFI_TCP_STATE_WAIT_PROMPT;
    wifi->tcp_deadline_ms = now_ms + BSP_WIFI_TCP_SEND_TIMEOUT_MS;
  }
  else if ((wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_PROMPT) &&
           (wifi->tcp_prompt_received != 0U))
  {
    wifi->tcp_prompt_received = 0U;
    if (HAL_UART_Transmit(wifi->uart, wifi->tcp_tx_buffer, wifi->tcp_tx_length,
                          BSP_WIFI_TX_TIMEOUT_MS) != HAL_OK)
    { BSP_WiFi_TcpFailed(wifi, BSP_WIFI_ERROR_TCP_SEND); return; }
    wifi->tcp_state = BSP_WIFI_TCP_STATE_WAIT_SEND_RESULT;
    wifi->tcp_deadline_ms = now_ms + BSP_WIFI_TCP_SEND_TIMEOUT_MS;
  }
}


/**
 * @brief  请求建立 MQTT 使用的单路 TCP 连接。
 * @param  wifi   Wi-Fi 句柄
 * @param  host   目标主机名或地址
 * @param  port   目标端口
 * @param  now_ms 当前系统毫秒时间
 * @retval HAL 状态
 */
HAL_StatusTypeDef BSP_WiFi_TcpConnect(BSP_WiFi_HandleTypeDef *wifi,
                                      const char *host, uint16_t port,
                                      uint32_t now_ms)
{
  size_t length;
  if ((wifi == NULL) || (host == NULL) || (port == 0U) ||
      (wifi->state != BSP_WIFI_STATE_ONLINE)) return HAL_ERROR;
  length = strlen(host);
  if ((length == 0U) || (length > BSP_WIFI_TCP_HOST_MAX_LENGTH))
  { wifi->last_error = BSP_WIFI_ERROR_TCP_ARGUMENT; return HAL_ERROR; }
  if ((wifi->tcp_state != BSP_WIFI_TCP_STATE_CLOSED) &&
      (wifi->tcp_state != BSP_WIFI_TCP_STATE_ERROR)) return HAL_BUSY;
  BSP_WiFi_ResetTcp(wifi);
  (void)memcpy(wifi->tcp_host, host, length + 1U);
  wifi->tcp_port = port;
  wifi->tcp_connect_attempt_count++;
  wifi->tcp_state = BSP_WIFI_TCP_STATE_CONNECT_PENDING;
  wifi->tcp_deadline_ms = now_ms;
  return HAL_OK;
}


HAL_StatusTypeDef BSP_WiFi_TcpSend(BSP_WiFi_HandleTypeDef *wifi,
                                   const uint8_t *data, uint16_t length,
                                   uint32_t now_ms)
{
  if ((wifi == NULL) || (data == NULL) || (length == 0U) ||
      (length > BSP_WIFI_TCP_TX_BUFFER_SIZE)) return HAL_ERROR;
  if (wifi->tcp_state != BSP_WIFI_TCP_STATE_OPEN) return HAL_BUSY;
  (void)memcpy(wifi->tcp_tx_buffer, data, length);
  wifi->tcp_tx_length = length;
  wifi->tcp_state = BSP_WIFI_TCP_STATE_SEND_PENDING;
  wifi->tcp_deadline_ms = now_ms;
  return HAL_OK;
}


HAL_StatusTypeDef BSP_WiFi_TcpClose(BSP_WiFi_HandleTypeDef *wifi)
{
  HAL_StatusTypeDef status = HAL_OK;
  static const char close_command[] = "AT+CIPCLOSE\r\n";
  if (wifi == NULL) return HAL_ERROR;
  if ((wifi->uart != NULL) && (wifi->state == BSP_WIFI_STATE_ONLINE) &&
      (wifi->tcp_state != BSP_WIFI_TCP_STATE_CLOSED))
    status = HAL_UART_Transmit(wifi->uart, (const uint8_t *)close_command,
                               sizeof(close_command) - 1U, BSP_WIFI_TX_TIMEOUT_MS);
  BSP_WiFi_ResetTcp(wifi);
  return status;
}


uint16_t BSP_WiFi_TcpRead(BSP_WiFi_HandleTypeDef *wifi,
                          uint8_t *data, uint16_t maximum_length)
{
  uint16_t count = 0U;
  if ((wifi == NULL) || (data == NULL)) return 0U;
  while ((count < maximum_length) && (wifi->tcp_rx_tail != wifi->tcp_rx_head))
  {
    data[count++] = wifi->tcp_rx_ring[wifi->tcp_rx_tail];
    wifi->tcp_rx_tail = (uint16_t)((wifi->tcp_rx_tail + 1U) % BSP_WIFI_TCP_RX_RING_SIZE);
  }
  return count;
}
