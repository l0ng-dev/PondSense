/**
 * @file    esp01s_at.c
 * @brief   ESP-01S AT 命令发送与响应解析。
 *
 * @details
 * 解析 READY、ERROR、入网结果和 TCP 提示行，并更新 Wi-Fi 状态机共享字段。
 */

#include "esp01s_internal.h"
#include <string.h>

static uint8_t BSP_WiFi_ParseJoinErrorCode(const char *line)
{
  const char *value = strstr(line, "+CWJAP:");
  uint16_t code = 0U;

  if (value == NULL)
  {
    return 0U;
  }

  value += 7;
  while ((*value == ' ') || (*value == '\t'))
  {
    value++;
  }
  while ((*value >= '0') && (*value <= '9'))
  {
    code = (uint16_t)((code * 10U) + (uint16_t)(*value - '0'));
    if (code > 255U)
    {
      return 255U;
    }
    value++;
  }

  return (uint8_t)code;
}


static uint8_t BSP_WiFi_ParseSignedInteger(const char *text,
                                           int32_t *value,
                                           const char **end)
{
  int32_t parsed = 0;
  int32_t sign = 1;
  uint8_t has_digit = 0U;

  if ((text == NULL) || (value == NULL))
  {
    return 0U;
  }
  if (*text == '-')
  {
    sign = -1;
    text++;
  }
  while ((*text >= '0') && (*text <= '9'))
  {
    has_digit = 1U;
    parsed = (parsed * 10L) + (int32_t)(*text - '0');
    text++;
  }
  if (has_digit == 0U)
  {
    return 0U;
  }

  *value = parsed * sign;
  if (end != NULL)
  {
    *end = text;
  }
  return 1U;
}


static uint8_t BSP_WiFi_ParseScanLine(const BSP_WiFi_HandleTypeDef *wifi,
                                      const char *line,
                                      int16_t *rssi,
                                      uint8_t *channel,
                                      uint8_t *target_match)
{
  /* 按 +CWLAP:<ecn>,<ssid>,<rssi>,<mac>,<channel> 提取本项目需要的字段。 */
  const char *ssid_begin;
  const char *ssid_end;
  const char *value;
  const char *bssid_begin;
  const char *bssid_end;
  int32_t parsed_rssi;
  int32_t parsed_channel;
  size_t target_length;

  if ((wifi == NULL) || (line == NULL) || (rssi == NULL) ||
      (channel == NULL) || (target_match == NULL) ||
      (strncmp(line, "+CWLAP:", 7U) != 0))
  {
    return 0U;
  }

  ssid_begin = strchr(line, '"');
  if (ssid_begin == NULL)
  {
    return 0U;
  }
  ssid_begin++;
  ssid_end = strchr(ssid_begin, '"');
  if (ssid_end == NULL)
  {
    return 0U;
  }

  target_length = strlen(wifi->ssid);
  *target_match =
      (((size_t)(ssid_end - ssid_begin) == target_length) &&
       (strncmp(ssid_begin, wifi->ssid, target_length) == 0)) ? 1U : 0U;

  value = ssid_end + 1;
  if (*value != ',')
  {
    return 0U;
  }
  value++;
  if (BSP_WiFi_ParseSignedInteger(value, &parsed_rssi, &value) == 0U)
  {
    return 0U;
  }

  bssid_begin = strchr(value, '"');
  if (bssid_begin == NULL)
  {
    return 0U;
  }
  bssid_end = strchr(bssid_begin + 1, '"');
  if ((bssid_end == NULL) || (bssid_end[1] != ','))
  {
    return 0U;
  }
  if (BSP_WiFi_ParseSignedInteger(bssid_end + 2,
                                  &parsed_channel,
                                  NULL) == 0U)
  {
    return 0U;
  }
  if ((parsed_rssi < -127L) || (parsed_rssi > 0L) ||
      (parsed_channel < 1L) || (parsed_channel > 14L))
  {
    return 0U;
  }

  *rssi = (int16_t)parsed_rssi;
  *channel = (uint8_t)parsed_channel;
  return 1U;
}


/**
 * @brief  发送一条 AT 命令并登记响应超时。
 * @param  wifi      Wi-Fi 句柄
 * @param  command   以 CRLF 结尾的 AT 命令
 * @param  now_ms    当前系统毫秒时间
 * @param  timeout_ms 响应超时时间
 * @retval HAL 状态
 */
HAL_StatusTypeDef BSP_WiFi_SendCommand(BSP_WiFi_HandleTypeDef *wifi,
                                              const char *command,
                                              uint32_t now_ms,
                                              uint32_t timeout_ms)
{
  size_t length = strlen(command);
  HAL_StatusTypeDef status;

  status = HAL_UART_Transmit(wifi->uart,
                             (const uint8_t *)command,
                             (uint16_t)length,
                             BSP_WIFI_TX_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    BSP_WiFi_CommandFailed(wifi, BSP_WIFI_ERROR_UART_TX, now_ms);
    return status;
  }

  wifi->command_tx_count++;
  wifi->awaiting_response = 1U;
  wifi->command_deadline_ms = now_ms + timeout_ms;
  return HAL_OK;
}


void BSP_WiFi_HandleLine(BSP_WiFi_HandleTypeDef *wifi,
                         const char *line,
                         uint32_t now_ms)
{
  int16_t scan_rssi;
  uint8_t scan_channel;
  uint8_t target_match;
  if (line[0] == '\0') return;
  /* 异步链路通知优先处理，避免被当前 AT 命令的通用 OK/ERROR 分支吞掉。 */
  if ((strcmp(line, "CONNECT") == 0) ||
      (strstr(line, "ALREADY CONNECTED") != NULL))
  {
    if (wifi->tcp_state == BSP_WIFI_TCP_STATE_CONNECTING)
    {
      wifi->tcp_state = BSP_WIFI_TCP_STATE_OPEN;
      wifi->tcp_connect_count++;
      wifi->last_error = BSP_WIFI_ERROR_NONE;
    }
    return;
  }
  if (strcmp(line, "CLOSED") == 0) { BSP_WiFi_ResetTcp(wifi); return; }
  if (strcmp(line, "SEND OK") == 0)
  {
    if (wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_SEND_RESULT)
    {
      wifi->tcp_state = BSP_WIFI_TCP_STATE_OPEN;
      wifi->tcp_send_count++;
      wifi->last_error = BSP_WIFI_ERROR_NONE;
    }
    return;
  }
  if (strstr(line, "WIFI DISCONNECT") != NULL)
  {
    BSP_WiFi_ResetTcp(wifi);
    if (wifi->state == BSP_WIFI_STATE_ONLINE) BSP_WiFi_EnterBackoff(wifi, now_ms);
    return;
  }
  if ((wifi->state == BSP_WIFI_STATE_SCAN_TARGET_AP) &&
      (strncmp(line, "+CWLAP:", 7U) == 0))
  {
    wifi->scan_ap_count++;
    if (BSP_WiFi_ParseScanLine(wifi, line, &scan_rssi, &scan_channel,
                               &target_match) == 0U)
    {
      wifi->scan_parse_error_count++;
    }
    else if ((target_match != 0U) &&
             ((wifi->target_ap_found == 0U) ||
              (scan_rssi > wifi->target_ap_rssi)))
    {
      wifi->target_ap_found = 1U;
      wifi->target_ap_rssi = scan_rssi;
      (void)scan_channel;
    }
    return;
  }
  if ((wifi->state == BSP_WIFI_STATE_JOINING) &&
      (strstr(line, "+CWJAP:") != NULL))
  {
    wifi->join_error_code = BSP_WiFi_ParseJoinErrorCode(line);
    wifi->last_error = BSP_WIFI_ERROR_JOIN_FAILED;
  }
  if (strstr(line, "WIFI GOT IP") != NULL)
  {
    wifi->got_ip = 1U;
    if (wifi->state == BSP_WIFI_STATE_JOINING)
    {
      wifi->state = BSP_WIFI_STATE_ONLINE;
      wifi->awaiting_response = 0U;
      wifi->state_retry_count = 0U;
      wifi->backoff_exponent = 0U;
      wifi->last_error = BSP_WIFI_ERROR_NONE;
    }
    return;
  }
  if (strcmp(line, "ready") == 0)
  {
    BSP_WiFi_SetState(wifi, BSP_WIFI_STATE_SYNC, now_ms);
    return;
  }
  if ((wifi->awaiting_response != 0U) && (strcmp(line, "OK") == 0))
  {
    BSP_WiFi_CommandSucceeded(wifi, now_ms);
  }
  else if ((wifi->awaiting_response != 0U) &&
           ((strstr(line, "ERROR") != NULL) || (strstr(line, "FAIL") != NULL)))
  {
    if (wifi->state == BSP_WIFI_STATE_DISCONNECT_AP)
      BSP_WiFi_CommandSucceeded(wifi, now_ms);
    else if ((wifi->state == BSP_WIFI_STATE_JOINING) &&
             (wifi->join_error_code != 0U))
      BSP_WiFi_CommandFailed(wifi, BSP_WIFI_ERROR_JOIN_FAILED, now_ms);
    else
      BSP_WiFi_CommandFailed(wifi, BSP_WIFI_ERROR_RESPONSE, now_ms);
  }
  else if ((wifi->awaiting_response != 0U) && (strstr(line, "busy") != NULL))
  {
    BSP_WiFi_CommandFailed(wifi, BSP_WIFI_ERROR_BUSY, now_ms);
  }
  if (((wifi->tcp_state == BSP_WIFI_TCP_STATE_CONNECTING) ||
       (wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_PROMPT) ||
       (wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_SEND_RESULT)) &&
      ((strstr(line, "ERROR") != NULL) || (strstr(line, "FAIL") != NULL) ||
       (strstr(line, "link is not valid") != NULL)))
  {
    BSP_WiFi_TcpFailed(wifi,
        (wifi->tcp_state == BSP_WIFI_TCP_STATE_CONNECTING)
            ? BSP_WIFI_ERROR_TCP_CONNECT : BSP_WIFI_ERROR_TCP_SEND);
  }
}


/**
 * @brief  消费 UART 环形缓冲中的完整 AT 响应行。
 * @param  wifi   Wi-Fi 句柄
 * @param  now_ms 当前系统毫秒时间
 */
void BSP_WiFi_ProcessReceivedData(BSP_WiFi_HandleTypeDef *wifi,
                                  uint32_t now_ms)
{
  /* 同一字节流中交错存在 AT 文本行和 +IPD 正文，按模式分别组帧。 */
  while (wifi->rx_tail != wifi->rx_head)
  {
    uint8_t received = wifi->rx_ring[wifi->rx_tail];
    wifi->rx_tail = (uint16_t)((wifi->rx_tail + 1U) % BSP_WIFI_RX_RING_SIZE);
    if (wifi->tcp_ipd_mode == 1U)
    {
      if ((received >= (uint8_t)'0') && (received <= (uint8_t)'9'))
      {
        uint32_t length = (uint32_t)wifi->tcp_ipd_length * 10UL +
                          (uint32_t)(received - (uint8_t)'0');
        if (length <= UINT16_MAX) wifi->tcp_ipd_length = (uint16_t)length;
        else { wifi->tcp_ipd_mode = 0U; wifi->tcp_ipd_length = 0U; }
      }
      else if ((received == (uint8_t)':') && (wifi->tcp_ipd_length > 0U))
      { wifi->tcp_ipd_mode = 2U; wifi->tcp_ipd_received = 0U; }
      else if ((received == (uint8_t)',') && (wifi->tcp_ipd_length > 0U))
      { wifi->tcp_ipd_mode = 3U; }
      else { wifi->tcp_ipd_mode = 0U; wifi->tcp_ipd_length = 0U; }
      continue;
    }
    if (wifi->tcp_ipd_mode == 3U)
    {
      if (received == (uint8_t)':')
      { wifi->tcp_ipd_mode = 2U; wifi->tcp_ipd_received = 0U; }
      continue;
    }
    if (wifi->tcp_ipd_mode == 2U)
    {
      BSP_WiFi_PushTcpRxByte(wifi, received);
      if (++wifi->tcp_ipd_received >= wifi->tcp_ipd_length)
      { wifi->tcp_ipd_mode = 0U; wifi->tcp_ipd_length = 0U; wifi->tcp_ipd_received = 0U; }
      continue;
    }
    if ((received == (uint8_t)'>') &&
        (wifi->tcp_state == BSP_WIFI_TCP_STATE_WAIT_PROMPT))
    { wifi->tcp_prompt_received = 1U; continue; }
    if (received == (uint8_t)'\n')
    {
      if (wifi->discarding_line != 0U)
      { wifi->discarding_line = 0U; wifi->line_length = 0U; }
      else if (wifi->line_length > 0U)
      {
        wifi->line[wifi->line_length] = '\0';
        BSP_WiFi_HandleLine(wifi, wifi->line, now_ms);
        wifi->line_length = 0U;
      }
    }
    else if (received != (uint8_t)'\r')
    {
      if ((wifi->discarding_line == 0U) &&
          (wifi->line_length < (BSP_WIFI_LINE_SIZE - 1U)))
      {
        wifi->line[wifi->line_length++] = (char)received;
        if ((wifi->line_length == 5U) && (memcmp(wifi->line, "+IPD,", 5U) == 0))
        { wifi->line_length = 0U; wifi->tcp_ipd_length = 0U; wifi->tcp_ipd_mode = 1U; }
      }
      else if (wifi->discarding_line == 0U)
      { wifi->line_length = 0U; wifi->discarding_line = 1U; wifi->last_error = BSP_WIFI_ERROR_RX_OVERFLOW; }
    }
  }
}
