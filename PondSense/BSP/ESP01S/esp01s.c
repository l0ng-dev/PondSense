/** @file esp01s.c @brief ESP-01S Wi-Fi 连接状态机。 */

#include "esp01s_internal.h"
#include <stdio.h>
#include <string.h>

static uint8_t BSP_WiFi_TimeReached(uint32_t now_ms, uint32_t deadline_ms)
{
  /* 有符号差值可在 HAL tick 回绕后继续正确比较短期截止时间。 */
  return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1U : 0U;
}


static uint8_t BSP_WiFi_IsCredentialValid(const char *text,
                                           uint16_t maximum_length,
                                           uint8_t allow_empty)
{
  size_t length;
  size_t index;

  if (text == NULL)
  {
    return 0U;
  }

  length = strlen(text);
  if (((length == 0U) && (allow_empty == 0U)) ||
      (length > maximum_length))
  {
    return 0U;
  }

  for (index = 0U; index < length; index++)
  {
    if ((text[index] == '"') || (text[index] == '\\') ||
        (text[index] == '\r') || (text[index] == '\n'))
    {
      return 0U;
    }
  }

  return 1U;
}


static HAL_StatusTypeDef BSP_WiFi_StartReceive(BSP_WiFi_HandleTypeDef *wifi)
{
  HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(
      wifi->uart, wifi->rx_chunk, BSP_WIFI_RX_CHUNK_SIZE);
  if (status != HAL_OK)
  {
    wifi->last_error = BSP_WIFI_ERROR_UART_RX_START;
    wifi->rx_restart_pending = 1U;
  }
  else
  {
    wifi->rx_dma_position = 0U;
    if (wifi->uart->hdmarx != NULL)
    {
      __HAL_DMA_DISABLE_IT(wifi->uart->hdmarx, DMA_IT_HT);
    }
    wifi->rx_restart_pending = 0U;
  }
  return status;
}


void BSP_WiFi_SetState(BSP_WiFi_HandleTypeDef *wifi,
                              BSP_WiFi_StateTypeDef state,
                              uint32_t next_action_ms)
{
  wifi->state = state;
  wifi->awaiting_response = 0U;
  wifi->state_retry_count = 0U;
  wifi->next_action_ms = next_action_ms;
}


void BSP_WiFi_EnterBackoff(BSP_WiFi_HandleTypeDef *wifi,
                                  uint32_t now_ms)
{
  uint32_t delay_ms = 1000U << wifi->backoff_exponent;

  wifi->reconnect_count++;

  /* 指数退避设置上限，避免断网时持续高频发送 AT 命令。 */
  if (delay_ms > BSP_WIFI_MAX_BACKOFF_MS)
  {
    delay_ms = BSP_WIFI_MAX_BACKOFF_MS;
  }
  if (wifi->backoff_exponent < 4U)
  {
    wifi->backoff_exponent++;
  }

  wifi->state = BSP_WIFI_STATE_BACKOFF;
  wifi->awaiting_response = 0U;
  wifi->state_retry_count = 0U;
  wifi->got_ip = 0U;
  BSP_WiFi_ResetTcp(wifi);
  wifi->next_action_ms = now_ms + delay_ms;
}


void BSP_WiFi_CommandSucceeded(BSP_WiFi_HandleTypeDef *wifi,
                                      uint32_t now_ms)
{
  wifi->command_success_count++;
  wifi->awaiting_response = 0U;
  wifi->last_error = BSP_WIFI_ERROR_NONE;

  switch (wifi->state)
  {
    case BSP_WIFI_STATE_SYNC:
      BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_ECHO_OFF, now_ms + 50U);
      break;

    case BSP_WIFI_STATE_ECHO_OFF:
      BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_SET_STATION_MODE, now_ms + 50U);
      break;

    case BSP_WIFI_STATE_SET_STATION_MODE:
      BSP_WiFi_SetState(wifi,
                        BSP_WIFI_STATE_SET_SINGLE_CONNECTION,
                        now_ms + 50U);
      break;

    case BSP_WIFI_STATE_SET_SINGLE_CONNECTION:
      if ((wifi->connect_requested != 0U) &&
          (wifi->ssid_set != 0U) && (wifi->password_set != 0U))
      {
        BSP_WiFi_SetState(wifi,
                          (wifi->auto_connect_disabled != 0U) ?
                              BSP_WIFI_STATE_DISCONNECT_AP :
                              BSP_WIFI_STATE_DISABLE_AUTO_CONNECT,
                          now_ms + 50U);
      }
      else
      {
        BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_WAIT_CREDENTIALS, now_ms);
      }
      break;

    case BSP_WIFI_STATE_DISABLE_AUTO_CONNECT:
      wifi->auto_connect_disabled = 1U;
      BSP_WiFi_SetState(wifi,
                        BSP_WIFI_STATE_DISCONNECT_AP,
                        now_ms + 50U);
      break;

    case BSP_WIFI_STATE_DISCONNECT_AP:
      BSP_WiFi_SetState(wifi,
                        BSP_WIFI_STATE_SCAN_TARGET_AP,
                        now_ms + 50U);
      break;

    case BSP_WIFI_STATE_SCAN_TARGET_AP:
      if (wifi->target_ap_found != 0U)
      {
        BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_JOINING, now_ms + 50U);
      }
      else
      {
        BSP_WiFi_CommandFailed(wifi, BSP_WIFI_ERROR_AP_NOT_FOUND, now_ms);
      }
      break;

    case BSP_WIFI_STATE_JOINING:
      wifi->state = BSP_WIFI_STATE_ONLINE;
      wifi->got_ip = 1U;
      wifi->state_retry_count = 0U;
      wifi->backoff_exponent = 0U;
      break;

    default:
      break;
  }
}


void BSP_WiFi_CommandFailed(BSP_WiFi_HandleTypeDef *wifi,
                                   BSP_WiFi_ErrorTypeDef error,
                                   uint32_t now_ms)
{
  wifi->command_failure_count++;
  wifi->awaiting_response = 0U;
  wifi->last_error = error;
  wifi->state_retry_count++;

  if (wifi->state_retry_count <= BSP_WIFI_MAX_STATE_RETRIES)
  {
    wifi->next_action_ms = now_ms + BSP_WIFI_RETRY_DELAY_MS;
  }
  else
  {
    BSP_WiFi_EnterBackoff(wifi, now_ms);
  }
}


/**
 * @brief  初始化 ESP-01S Wi-Fi 状态机和 UART DMA 接收。
 * @param  wifi   Wi-Fi 句柄
 * @param  uart   ESP-01S 所在 UART 句柄
 * @param  now_ms 当前系统毫秒时间
 * @retval HAL 状态
 */
HAL_StatusTypeDef BSP_WiFi_Init(BSP_WiFi_HandleTypeDef *wifi,
                                UART_HandleTypeDef *uart,
                                uint32_t now_ms)
{
  if ((wifi == NULL) || (uart == NULL))
  {
    return HAL_ERROR;
  }
  (void)memset(wifi, 0, sizeof(*wifi));
  wifi->uart = uart;
  wifi->state = BSP_WIFI_STATE_WAIT_READY;
  wifi->next_action_ms = now_ms + BSP_WIFI_READY_WAIT_MS;
  BSP_WiFi_ResetTcp(wifi);
  if (BSP_WiFi_StartReceive(wifi) != HAL_OK)
  {
    wifi->state = BSP_WIFI_STATE_ERROR;
    return HAL_ERROR;
  }
  return HAL_OK;
}


/**
 * @brief  非阻塞推进 Wi-Fi AT、入网和 TCP 状态机。
 * @param  wifi   Wi-Fi 句柄
 * @param  now_ms 当前系统毫秒时间
 * @note   不在此函数中等待模块响应，超时通过状态机处理。
 */
void BSP_WiFi_Process(BSP_WiFi_HandleTypeDef *wifi, uint32_t now_ms)
{
  char command[BSP_WIFI_SSID_MAX_LENGTH + BSP_WIFI_PASSWORD_MAX_LENGTH + 32U];
  if ((wifi == NULL) || (wifi->uart == NULL))
  {
    return;
  }
  if (wifi->rx_restart_pending != 0U)
  {
    (void)HAL_UART_AbortReceive(wifi->uart);
    if (BSP_WiFi_StartReceive(wifi) != HAL_OK)
    {
      return;
    }
    if (wifi->state == BSP_WIFI_STATE_ERROR)
    {
      BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_SYNC, now_ms);
    }
  }
  BSP_WiFi_ProcessReceivedData(wifi, now_ms);
  if ((wifi->awaiting_response != 0U) &&
      (BSP_WiFi_TimeReached(now_ms, wifi->command_deadline_ms) != 0U))
  {
    BSP_WiFi_CommandFailed(wifi, BSP_WIFI_ERROR_TIMEOUT, now_ms);
  }
  if ((wifi->awaiting_response == 0U) &&
      (BSP_WiFi_TimeReached(now_ms, wifi->next_action_ms) != 0U))
  {
    switch (wifi->state)
    {
      case BSP_WIFI_STATE_WAIT_READY:
        BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_SYNC, now_ms);
        break;
      case BSP_WIFI_STATE_SYNC:
        (void)BSP_WiFi_SendCommand(wifi, "AT\r\n", now_ms,
                                   BSP_WIFI_COMMAND_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_ECHO_OFF:
        (void)BSP_WiFi_SendCommand(wifi, "ATE0\r\n", now_ms,
                                   BSP_WIFI_COMMAND_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_SET_STATION_MODE:
        (void)BSP_WiFi_SendCommand(wifi, "AT+CWMODE_CUR=1\r\n", now_ms,
                                   BSP_WIFI_COMMAND_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_SET_SINGLE_CONNECTION:
        (void)BSP_WiFi_SendCommand(wifi, "AT+CIPMUX=0\r\n", now_ms,
                                   BSP_WIFI_COMMAND_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_DISABLE_AUTO_CONNECT:
        (void)BSP_WiFi_SendCommand(wifi, "AT+CWAUTOCONN=0\r\n", now_ms,
                                   BSP_WIFI_COMMAND_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_DISCONNECT_AP:
        (void)BSP_WiFi_SendCommand(wifi, "AT+CWQAP\r\n", now_ms,
                                   BSP_WIFI_COMMAND_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_SCAN_TARGET_AP:
        wifi->target_ap_found = 0U;
        wifi->target_ap_rssi = -127;
        wifi->scan_cycle_count++;
        wifi->scan_ap_count = 0U;
        wifi->scan_parse_error_count = 0U;
        (void)BSP_WiFi_SendCommand(wifi, "AT+CWLAP\r\n", now_ms,
                                   BSP_WIFI_SCAN_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_JOINING:
        if ((wifi->ssid_set == 0U) || (wifi->password_set == 0U))
        {
          BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_WAIT_CREDENTIALS, now_ms);
          wifi->last_error = BSP_WIFI_ERROR_CREDENTIALS;
          break;
        }
        wifi->join_error_code = 0U;
        (void)snprintf(command, sizeof(command),
                       "AT+CWJAP_CUR=\"%s\",\"%s\"\r\n",
                       wifi->ssid, wifi->password);
        (void)BSP_WiFi_SendCommand(wifi, command, now_ms,
                                   BSP_WIFI_JOIN_TIMEOUT_MS);
        break;
      case BSP_WIFI_STATE_WAIT_CREDENTIALS:
        if ((wifi->connect_requested != 0U) && (wifi->ssid_set != 0U) &&
            (wifi->password_set != 0U))
        {
          BSP_WiFi_SetState(wifi,
                            (wifi->auto_connect_disabled != 0U)
                                ? BSP_WIFI_STATE_DISCONNECT_AP
                                : BSP_WIFI_STATE_DISABLE_AUTO_CONNECT,
                            now_ms);
        }
        break;
      case BSP_WIFI_STATE_BACKOFF:
        BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_SYNC, now_ms);
        break;
      default:
        break;
    }
  }
  BSP_WiFi_ProcessTcp(wifi, now_ms);
}


HAL_StatusTypeDef BSP_WiFi_SetSSID(BSP_WiFi_HandleTypeDef *wifi,
                                   const char *ssid)
{
  if ((wifi == NULL) ||
      (BSP_WiFi_IsCredentialValid(ssid, BSP_WIFI_SSID_MAX_LENGTH, 0U) == 0U))
  {
    return HAL_ERROR;
  }
  (void)snprintf(wifi->ssid, sizeof(wifi->ssid), "%s", ssid);
  wifi->ssid_set = 1U;
  return HAL_OK;
}


HAL_StatusTypeDef BSP_WiFi_SetPassword(BSP_WiFi_HandleTypeDef *wifi,
                                       const char *password)
{
  if ((wifi == NULL) ||
      (BSP_WiFi_IsCredentialValid(password, BSP_WIFI_PASSWORD_MAX_LENGTH, 1U) == 0U))
  {
    return HAL_ERROR;
  }
  (void)snprintf(wifi->password, sizeof(wifi->password), "%s", password);
  wifi->password_set = 1U;
  return HAL_OK;
}


/**
 * @brief  请求连接已配置的目标热点。
 * @param  wifi   Wi-Fi 句柄
 * @param  now_ms 当前系统毫秒时间
 * @retval HAL 状态
 */
HAL_StatusTypeDef BSP_WiFi_Connect(BSP_WiFi_HandleTypeDef *wifi,
                                   uint32_t now_ms)
{
  if (wifi == NULL)
  {
    return HAL_ERROR;
  }
  if ((wifi->ssid_set == 0U) || (wifi->password_set == 0U))
  {
    wifi->last_error = BSP_WIFI_ERROR_CREDENTIALS;
    return HAL_ERROR;
  }
  wifi->connect_requested = 1U;
  wifi->got_ip = 0U;
  BSP_WiFi_SetState(wifi,
                    (wifi->auto_connect_disabled != 0U)
                        ? BSP_WIFI_STATE_DISCONNECT_AP
                        : BSP_WIFI_STATE_DISABLE_AUTO_CONNECT,
                    now_ms);
  return HAL_OK;
}


void BSP_WiFi_RxEventCallback(BSP_WiFi_HandleTypeDef *wifi,
                              UART_HandleTypeDef *uart,
                              uint16_t size)
{
  uint16_t index;
  uint16_t position;
  if ((wifi == NULL) || (uart != wifi->uart))
  {
    return;
  }
  if (size > BSP_WIFI_RX_CHUNK_SIZE)
  {
    size = BSP_WIFI_RX_CHUNK_SIZE;
  }
  position = wifi->rx_dma_position;
  if (size >= position)
  {
    for (index = position; index < size; index++)
    {
      uint16_t next = (uint16_t)((wifi->rx_head + 1U) % BSP_WIFI_RX_RING_SIZE);
      if (next == wifi->rx_tail)
      {
        wifi->last_error = BSP_WIFI_ERROR_RX_OVERFLOW;
        break;
      }
      wifi->rx_ring[wifi->rx_head] = wifi->rx_chunk[index];
      wifi->rx_head = next;
    }
  }
  else
  {
    for (index = position; index < BSP_WIFI_RX_CHUNK_SIZE; index++)
    {
      uint16_t next = (uint16_t)((wifi->rx_head + 1U) % BSP_WIFI_RX_RING_SIZE);
      if (next == wifi->rx_tail)
      {
        wifi->last_error = BSP_WIFI_ERROR_RX_OVERFLOW;
        break;
      }
      wifi->rx_ring[wifi->rx_head] = wifi->rx_chunk[index];
      wifi->rx_head = next;
    }
    for (index = 0U; index < size; index++)
    {
      uint16_t next = (uint16_t)((wifi->rx_head + 1U) % BSP_WIFI_RX_RING_SIZE);
      if (next == wifi->rx_tail)
      {
        wifi->last_error = BSP_WIFI_ERROR_RX_OVERFLOW;
        break;
      }
      wifi->rx_ring[wifi->rx_head] = wifi->rx_chunk[index];
      wifi->rx_head = next;
    }
  }
  wifi->rx_dma_position =
      (size == BSP_WIFI_RX_CHUNK_SIZE) ? 0U : size;
}


void BSP_WiFi_ErrorCallback(BSP_WiFi_HandleTypeDef *wifi,
                            UART_HandleTypeDef *uart)
{
  if ((wifi == NULL) || (uart != wifi->uart))
  {
    return;
  }
  wifi->rx_restart_pending = 1U;
  wifi->last_error = BSP_WIFI_ERROR_UART_RX_START;
}
