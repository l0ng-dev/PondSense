#include "network.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "k230_session.h"
#include "sensing.h"
#include "network_config.h"
#include "../modules/uart_links/uart_links.h"
#include "../protocol/mqtt/mqtt_codec.h"

#define NETWORK_LINE_CAPACITY       192U
#define NETWORK_PACKET_CAPACITY     256U
#define NETWORK_MQTT_RX_CAPACITY    256U
#define NETWORK_PAYLOAD_CAPACITY    160U
#define NETWORK_COMMAND_TIMEOUT_MS  1500U
#define NETWORK_JOIN_TIMEOUT_MS     20000U
#define NETWORK_TCP_TIMEOUT_MS      10000U
#define NETWORK_SEND_TIMEOUT_MS      3000U
#define NETWORK_MQTT_TIMEOUT_MS      5000U
#define NETWORK_MQTT_SKIP_TIMEOUT_MS 5000U
#define NETWORK_PING_INTERVAL_MS    30000U

app_network_status_t g_app_network;

static char s_line[NETWORK_LINE_CAPACITY];
static uint16_t s_line_length;
static bool s_line_discard;
static uint32_t s_ipd_remaining;
static uint8_t s_mqtt_rx[NETWORK_MQTT_RX_CAPACITY];
static uint16_t s_mqtt_rx_length;
static uint32_t s_mqtt_skip_remaining;
static uint32_t s_mqtt_skip_start_ms;
static uint8_t s_packet[NETWORK_PACKET_CAPACITY];
static uint16_t s_packet_length;
static uint8_t s_packet_phase; /* 0: 未发 CIPSEND；1: 等 >；2: 等 SEND OK。 */
static bool s_early_ack_pending;
static uint8_t s_early_ack_packet[4]; /* 固定头与最多 3 字节应答内容。 */
static uint16_t s_early_ack_remaining;
static bool s_prompt_seen;
static bool s_command_waiting;
static uint32_t s_deadline_ms;
static uint32_t s_next_action_ms;
static uint32_t s_next_publish_ms;
static uint32_t s_last_tx_ms;
static uint32_t s_now_ms;
static uint32_t s_rx_drops_seen;
static uint32_t s_rx_faults_seen;
static uint8_t s_backoff_exponent;
static uint16_t s_packet_id;
static char s_command[NETWORK_LINE_CAPACITY];
static char s_payload[NETWORK_PAYLOAD_CAPACITY];

static bool network_time_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t) (now_ms - deadline_ms) >= 0;
}

static bool network_safe_at_field(char const * text, size_t maximum)
{
    size_t const length = strlen(text);
    if ((0U == length) || (length > maximum))
    {
        return false;
    }
    for (size_t index = 0U; index < length; ++index)
    {
        if (('"' == text[index]) || ('\\' == text[index]) ||
            ('\r' == text[index]) || ('\n' == text[index]))
        {
            return false;
        }
    }
    return true;
}

static void network_set_state(app_network_state_t state)
{
    g_app_network.state = state;
    s_command_waiting = false;
    s_packet_phase = 0U;
    s_packet_length = 0U;
    s_prompt_seen = false;
}

static void network_backoff(app_network_error_t error)
{
    uint32_t delay_ms = 1000U << s_backoff_exponent;
    if (delay_ms > 10000U)
    {
        delay_ms = 10000U;
    }
    if (s_backoff_exponent < 4U)
    {
        ++s_backoff_exponent;
    }
    ++g_app_network.reconnect_count;
    g_app_network.last_error = error;
    g_app_network.last_failed_state = g_app_network.state;
    s_next_action_ms = s_now_ms + delay_ms;
    s_line_length = 0U;
    s_line_discard = false;
    s_ipd_remaining = 0U;
    s_mqtt_rx_length = 0U;
    s_mqtt_skip_remaining = 0U;
    s_mqtt_skip_start_ms = 0U;
    s_early_ack_pending = false;
    network_set_state(APP_NETWORK_BACKOFF);
}

static void network_send_command(char const * command, uint32_t timeout_ms)
{
    fsp_err_t const result = Uart_Links_EspWrite((uint8_t const *) command,
                                               (uint16_t) strlen(command));
    if (FSP_ERR_IN_USE == result)
    {
        return;
    }
    if (FSP_SUCCESS != result)
    {
        network_backoff(APP_NETWORK_ERROR_UART);
        return;
    }
    ++g_app_network.at_send_count;
    s_command_waiting = true;
    s_deadline_ms = s_now_ms + timeout_ms;
}

static void network_mqtt_handle_packet(uint8_t const * packet,
                                        uint16_t header_length,
                                        uint16_t remaining)
{
    uint8_t const type = (uint8_t) (packet[0] >> 4);
    uint8_t const * body = &packet[header_length];
    if ((2U == s_packet_phase) &&
        (((2U == type) && (APP_NETWORK_MQTT_SEND_CONNECT == g_app_network.state)) ||
         ((9U == type) && (APP_NETWORK_MQTT_SEND_SUBSCRIBE == g_app_network.state)) ||
         ((13U == type) && (APP_NETWORK_MQTT_SEND_PING == g_app_network.state))))
    {
        /* ESP 可能先交付 +IPD，再报告 SEND OK；应答留到发送确认后处理。 */
        s_early_ack_packet[0] = packet[0];
        uint16_t const copy_length = (remaining < 3U) ? remaining : 3U;
        memcpy(&s_early_ack_packet[1], body, copy_length);
        s_early_ack_remaining = remaining;
        s_early_ack_pending = true;
        return;
    }
    if ((2U == type) && (APP_NETWORK_MQTT_WAIT_CONNACK == g_app_network.state))
    {
        /* 巴法云可能置 Session Present；F407 仅以返回码判断成功。 */
        if ((2U != remaining) || (body[0] > 1U))
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
            return;
        }
        g_app_network.mqtt_connack_code = body[1];
        if (0U != body[1])
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_CONNACK);
            return;
        }
        ++g_app_network.mqtt_connect_count;
        network_set_state(APP_NETWORK_MQTT_SEND_SUBSCRIBE);
    }
    else if ((9U == type) && (APP_NETWORK_MQTT_WAIT_SUBACK == g_app_network.state))
    {
        if ((3U > remaining) || (body[0] != (uint8_t) (s_packet_id >> 8)) ||
            (body[1] != (uint8_t) s_packet_id))
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
            return;
        }
        g_app_network.mqtt_suback_code = body[2];
        if (body[2] > 2U)
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_SUBACK);
            return;
        }
        ++g_app_network.mqtt_subscribe_count;
        g_app_network.last_online_ms = s_now_ms;
        g_app_network.last_error = APP_NETWORK_ERROR_NONE;
        s_backoff_exponent = 0U;
        s_next_publish_ms = s_now_ms + APP_NETWORK_TELEMETRY_INTERVAL_MS;
        network_set_state(APP_NETWORK_ONLINE);
    }
    else if ((13U == type) && (APP_NETWORK_MQTT_WAIT_PINGRESP == g_app_network.state))
    {
        if ((0xD0U != packet[0]) || (0U != remaining))
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
        }
        else
        {
            network_set_state(APP_NETWORK_ONLINE);
        }
    }
    else if ((3U == type) && (remaining >= 2U))
    {
        uint16_t const topic_length = (uint16_t) (((uint16_t) body[0] << 8) | body[1]);
        uint32_t offset = 2U + topic_length;
        if (0U != (packet[0] & 0x06U))
        {
            offset += 2U;
        }
        if (offset <= remaining)
        {
            uint16_t const message_length = (uint16_t) (remaining - offset);
            uint16_t const copy_length = (message_length < sizeof(g_app_network.last_downlink)) ?
                                         message_length :
                                         (uint16_t) (sizeof(g_app_network.last_downlink) - 1U);
            memcpy(g_app_network.last_downlink, &body[offset], copy_length);
            g_app_network.last_downlink[copy_length] = '\0';
            ++g_app_network.mqtt_rx_publish_count;
        }
    }
}

static void network_mqtt_receive_byte(uint8_t value)
{
    if (s_mqtt_skip_remaining > 0U)
    {
        --s_mqtt_skip_remaining;
        return;
    }
    if (s_mqtt_rx_length >= sizeof(s_mqtt_rx))
    {
        ++g_app_network.rx_overflow_count;
        network_backoff(APP_NETWORK_ERROR_RX_OVERFLOW);
        return;
    }
    s_mqtt_rx[s_mqtt_rx_length++] = value;
    while (s_mqtt_rx_length >= 2U)
    {
        uint32_t packet_length;
        uint16_t header_length;
        uint32_t remaining;
        pondsense_mqtt_frame_status_t const frame_status = PondSense_Mqtt_PeekFrame(
            s_mqtt_rx, s_mqtt_rx_length, sizeof(s_mqtt_rx),
            &packet_length, &header_length, &remaining);
        if (PONDSENSE_MQTT_FRAME_OVERSIZE == frame_status)
        {
            if (((s_mqtt_rx[0] & 0xF0U) == 0x30U) &&
                ((s_mqtt_rx[0] & 0x06U) == 0U))
            {
                /* QoS 0 下行只用于展示；跳过本帧后继续解析同一 TCP 流。 */
                s_mqtt_skip_remaining = packet_length - s_mqtt_rx_length;
                s_mqtt_skip_start_ms = s_now_ms;
                s_mqtt_rx_length = 0U;
                ++g_app_network.mqtt_oversize_publish_count;
                return;
            }
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
            return;
        }
        if (PONDSENSE_MQTT_FRAME_INVALID == frame_status)
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
            return;
        }
        if (PONDSENSE_MQTT_FRAME_INCOMPLETE == frame_status)
        {
            return;
        }
        network_mqtt_handle_packet(s_mqtt_rx, header_length, (uint16_t) remaining);
        if (APP_NETWORK_BACKOFF == g_app_network.state)
        {
            return;
        }
        s_mqtt_rx_length = (uint16_t) (s_mqtt_rx_length - packet_length);
        memmove(s_mqtt_rx, &s_mqtt_rx[packet_length], s_mqtt_rx_length);
    }
}

static void network_command_succeeded(void)
{
    switch (g_app_network.state)
    {
        case APP_NETWORK_AT_SYNC:
            network_set_state(APP_NETWORK_ECHO_OFF);
            break;
        case APP_NETWORK_ECHO_OFF:
            network_set_state(APP_NETWORK_DISABLE_AUTO_CONNECT);
            break;
        case APP_NETWORK_DISABLE_AUTO_CONNECT:
            network_set_state(APP_NETWORK_DISCONNECT_AP);
            break;
        case APP_NETWORK_DISCONNECT_AP:
            network_set_state(APP_NETWORK_STATION_MODE);
            break;
        case APP_NETWORK_STATION_MODE:
            network_set_state(APP_NETWORK_SINGLE_CONNECTION);
            break;
        case APP_NETWORK_SINGLE_CONNECTION:
            network_set_state(APP_NETWORK_JOIN_AP);
            break;
        case APP_NETWORK_JOIN_AP:
            ++g_app_network.wifi_join_count;
            network_set_state(APP_NETWORK_TCP_CONNECT);
            break;
        case APP_NETWORK_TCP_CONNECT:
            network_set_state(APP_NETWORK_MQTT_SEND_CONNECT);
            break;
        default:
            return;
    }
    s_next_action_ms = s_now_ms;
}

static void network_packet_send_succeeded(void)
{
    s_last_tx_ms = s_now_ms;
    switch (g_app_network.state)
    {
        case APP_NETWORK_MQTT_SEND_CONNECT:
            network_set_state(APP_NETWORK_MQTT_WAIT_CONNACK);
            break;
        case APP_NETWORK_MQTT_SEND_SUBSCRIBE:
            network_set_state(APP_NETWORK_MQTT_WAIT_SUBACK);
            break;
        case APP_NETWORK_MQTT_SEND_PUBLISH:
            ++g_app_network.mqtt_publish_sent_count;
            network_set_state(APP_NETWORK_ONLINE);
            break;
        case APP_NETWORK_MQTT_SEND_PING:
            network_set_state(APP_NETWORK_MQTT_WAIT_PINGRESP);
            break;
        default:
            return;
    }
    s_deadline_ms = s_now_ms + NETWORK_MQTT_TIMEOUT_MS;
    if (s_early_ack_pending)
    {
        s_early_ack_pending = false;
        network_mqtt_handle_packet(s_early_ack_packet, 1U, s_early_ack_remaining);
    }
}

static void network_handle_line(char const * line)
{
    if (0 == strcmp(line, "WIFI DISCONNECT"))
    {
        /* CWQAP and CWJAP may report the old association dropping first. */
        if (s_command_waiting &&
            ((APP_NETWORK_DISCONNECT_AP == g_app_network.state) ||
             (APP_NETWORK_JOIN_AP == g_app_network.state)))
        {
            return;
        }
        network_backoff(APP_NETWORK_ERROR_RESPONSE);
        return;
    }
    if (0 == strcmp(line, "ready"))
    {
        network_backoff(APP_NETWORK_ERROR_RESPONSE);
        return;
    }
    if (0 == strcmp(line, "CLOSED"))
    {
        network_backoff(APP_NETWORK_ERROR_RESPONSE);
        return;
    }
    if ((2U == s_packet_phase) && (0 == strcmp(line, "SEND OK")))
    {
        network_packet_send_succeeded();
        return;
    }
    if ((0 == strcmp(line, "ERROR")) || (0 == strcmp(line, "FAIL")) ||
        (0 == strcmp(line, "SEND FAIL")) || (NULL != strstr(line, "busy")))
    {
        if ((APP_NETWORK_DISCONNECT_AP == g_app_network.state) &&
            s_command_waiting && (0 == strcmp(line, "ERROR")))
        {
            network_command_succeeded();
            return;
        }
        network_backoff(APP_NETWORK_ERROR_RESPONSE);
        return;
    }
    if (s_command_waiting && (0 == strcmp(line, "OK")))
    {
        network_command_succeeded();
    }
}

static bool network_parse_ipd_length(char const * header, uint32_t * length)
{
    if (0 != strncmp(header, "+IPD,", 5U))
    {
        return false;
    }
    char const * cursor = header + 5;
    uint32_t first = 0U;
    if ((*cursor < '0') || (*cursor > '9'))
    {
        return false;
    }
    while ((*cursor >= '0') && (*cursor <= '9'))
    {
        first = first * 10U + (uint32_t) (*cursor++ - '0');
        if (first > 65535U)
        {
            return false;
        }
    }
    if (',' == *cursor)
    {
        char const * second_cursor = cursor + 1;
        uint32_t second = 0U;
        bool second_numeric = (*second_cursor >= '0') && (*second_cursor <= '9');
        while ((*second_cursor >= '0') && (*second_cursor <= '9'))
        {
            second = second * 10U + (uint32_t) (*second_cursor++ - '0');
            if (second > 65535U)
            {
                return false;
            }
        }
        if (second_numeric && ('\0' == *second_cursor))
        {
            *length = second; /* +IPD,<link_id>,<length> */
            return true;
        }
    }
    if (('\0' != *cursor) && (',' != *cursor))
    {
        return false;
    }
    *length = first; /* +IPD,<length> 或带远端地址的形式 */
    return true;
}

static void network_receive_byte(uint8_t value)
{
    if (APP_NETWORK_BACKOFF == g_app_network.state)
    {
        return;
    }
    if (s_ipd_remaining > 0U)
    {
        --s_ipd_remaining;
        network_mqtt_receive_byte(value);
        return;
    }
    if (('>' == value) && (1U == s_packet_phase))
    {
        s_prompt_seen = true;
        return;
    }
    if (('\n' == value) || ('\r' == value))
    {
        if (('\n' == value) && (s_line_length > 0U) && !s_line_discard)
        {
            s_line[s_line_length] = '\0';
            network_handle_line(s_line);
        }
        if ('\n' == value)
        {
            s_line_length = 0U;
            s_line_discard = false;
        }
        return;
    }
    if ((':' == value) && (s_line_length >= 5U) &&
        (0 == memcmp(s_line, "+IPD,", 5U)))
    {
        uint32_t length;
        s_line[s_line_length] = '\0';
        if (!network_parse_ipd_length(s_line, &length))
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
            return;
        }
        s_ipd_remaining = length;
        s_line_length = 0U;
        return;
    }
    if (s_line_discard)
    {
        return;
    }
    if (s_line_length >= (sizeof(s_line) - 1U))
    {
        s_line_length = 0U;
        s_line_discard = true;
        ++g_app_network.rx_overflow_count;
        network_backoff(APP_NETWORK_ERROR_RX_OVERFLOW);
        return;
    }
    s_line[s_line_length++] = (char) value;
}

static void network_start_packet_send(void)
{
    if (0U == s_packet_length)
    {
        switch (g_app_network.state)
        {
            case APP_NETWORK_MQTT_SEND_CONNECT:
                s_packet_length = PondSense_Mqtt_BuildConnect(
                    s_packet, sizeof(s_packet), APP_NETWORK_MQTT_CLIENT_ID);
                break;
            case APP_NETWORK_MQTT_SEND_SUBSCRIBE:
                if (++s_packet_id == 0U)
                {
                    s_packet_id = 1U;
                }
                s_packet_length = PondSense_Mqtt_BuildSubscribe(
                    s_packet, sizeof(s_packet), s_packet_id, APP_NETWORK_MQTT_TOPIC);
                break;
            case APP_NETWORK_MQTT_SEND_PUBLISH:
            {
                char topic[64];
                int const written = snprintf(topic, sizeof(topic), "%s/up", APP_NETWORK_MQTT_TOPIC);
                if ((written > 0) && ((size_t) written < sizeof(topic)))
                {
                    s_packet_length = PondSense_Mqtt_BuildPublish(
                        s_packet, sizeof(s_packet), topic, s_payload);
                }
                break;
            }
            case APP_NETWORK_MQTT_SEND_PING:
                s_packet_length = PondSense_Mqtt_BuildPing(s_packet, sizeof(s_packet));
                break;
            default:
                return;
        }
        if (0U == s_packet_length)
        {
            network_backoff(APP_NETWORK_ERROR_MQTT_PACKET);
            return;
        }
    }
    if (0U == s_packet_phase)
    {
        int const length = snprintf(s_command, sizeof(s_command),
                                    "AT+CIPSEND=%u\r\n", (unsigned) s_packet_length);
        if ((length <= 0) || ((size_t) length >= sizeof(s_command)))
        {
            network_backoff(APP_NETWORK_ERROR_CONFIG);
            return;
        }
        fsp_err_t const result = Uart_Links_EspWrite((uint8_t const *) s_command,
                                                   (uint16_t) length);
        if (FSP_ERR_IN_USE == result)
        {
            return;
        }
        if (FSP_SUCCESS != result)
        {
            network_backoff(APP_NETWORK_ERROR_UART);
            return;
        }
        ++g_app_network.at_send_count;
        s_packet_phase = 1U;
        s_deadline_ms = s_now_ms + NETWORK_SEND_TIMEOUT_MS;
    }
    else if ((1U == s_packet_phase) && s_prompt_seen)
    {
        fsp_err_t const result = Uart_Links_EspWrite(s_packet, s_packet_length);
        if (FSP_ERR_IN_USE == result)
        {
            return;
        }
        if (FSP_SUCCESS != result)
        {
            network_backoff(APP_NETWORK_ERROR_UART);
            return;
        }
        s_prompt_seen = false;
        s_packet_phase = 2U;
        s_deadline_ms = s_now_ms + NETWORK_SEND_TIMEOUT_MS;
    }
}

static int network_format_telemetry(void)
{
    char temperature[20] = "null";
    char ph[16] = "null";
    char level[8] = "null";
    if (g_app_temperature.valid)
    {
        int32_t const centi = g_app_temperature.centi_c;
        int32_t const magnitude = (centi < 0) ? -centi : centi;
        (void) snprintf(temperature, sizeof(temperature), "\"%s%ld.%02ld℃\"",
                        (centi < 0) ? "-" : "",
                        (long) (magnitude / 100), (long) (magnitude % 100));
    }
    if (g_app_ph_adc.estimate_ready)
    {
        (void) snprintf(ph, sizeof(ph), "\"%u.%02u\"",
                        (unsigned) (g_app_ph_adc.estimate_centi / 100U),
                        (unsigned) (g_app_ph_adc.estimate_centi % 100U));
    }
    if ((g_app_k230.last_completed_event_id != 0U) &&
        (g_app_k230.last_result.valid != 0U) &&
        (g_app_k230.last_result.level <= 2U))
    {
        (void) snprintf(level, sizeof(level), "%u",
                        (unsigned) g_app_k230.last_result.level);
    }
    int const length = snprintf(s_payload, sizeof(s_payload),
                                "{\"水温\":%s,\"pH值\":%s,\"摄食等级\":%s}",
                                temperature, ph, level);
    return ((length > 0) && ((size_t) length < sizeof(s_payload))) ? length : -1;
}

void App_Network_Init(uint32_t now_ms)
{
    if (!network_safe_at_field(APP_NETWORK_WIFI_SSID, 32U) ||
        !network_safe_at_field(APP_NETWORK_WIFI_PASSWORD, 63U) ||
        !network_safe_at_field(APP_NETWORK_MQTT_HOST, 63U) ||
        (strlen(APP_NETWORK_MQTT_TOPIC) == 0U) ||
        (strlen(APP_NETWORK_MQTT_TOPIC) > 48U) ||
        (strlen(APP_NETWORK_MQTT_CLIENT_ID) == 0U) ||
        (strlen(APP_NETWORK_MQTT_CLIENT_ID) > 64U) ||
        (APP_NETWORK_MQTT_PORT == 0U) ||
        (APP_NETWORK_TELEMETRY_INTERVAL_MS < 1000U))
    {
        g_app_network.last_error = APP_NETWORK_ERROR_CONFIG;
        return;
    }
    if (g_uart_esp_rx.open_result != (uint32_t) FSP_SUCCESS)
    {
        g_app_network.last_error = APP_NETWORK_ERROR_UART;
        return;
    }
    s_now_ms = now_ms;
    s_rx_drops_seen = g_uart_esp_rx.ring_drops;
    s_rx_faults_seen = g_uart_esp_rx.rx_faults;
    s_next_action_ms = now_ms + 2000U;
    Uart_Links_RegisterEspHandler(network_receive_byte);
    network_set_state(APP_NETWORK_AT_SYNC);
}

void App_Network_Process(uint32_t now_ms)
{
    s_now_ms = now_ms;
    if (APP_NETWORK_DISABLED == g_app_network.state)
    {
        return;
    }
    if ((s_rx_drops_seen != g_uart_esp_rx.ring_drops) ||
        (s_rx_faults_seen != g_uart_esp_rx.rx_faults))
    {
        s_rx_drops_seen = g_uart_esp_rx.ring_drops;
        s_rx_faults_seen = g_uart_esp_rx.rx_faults;
        ++g_app_network.rx_overflow_count;
        network_backoff(APP_NETWORK_ERROR_RX_OVERFLOW);
    }
    if (APP_NETWORK_BACKOFF == g_app_network.state)
    {
        if (network_time_reached(now_ms, s_next_action_ms))
        {
            s_next_action_ms = now_ms;
            network_set_state(APP_NETWORK_AT_SYNC);
        }
        return;
    }
    if ((s_mqtt_skip_remaining > 0U) &&
        ((uint32_t) (now_ms - s_mqtt_skip_start_ms) >= NETWORK_MQTT_SKIP_TIMEOUT_MS))
    {
        ++g_app_network.timeout_count;
        network_backoff(APP_NETWORK_ERROR_TIMEOUT);
        return;
    }
    if ((s_command_waiting || (s_packet_phase > 0U) ||
         (APP_NETWORK_MQTT_WAIT_CONNACK == g_app_network.state) ||
         (APP_NETWORK_MQTT_WAIT_SUBACK == g_app_network.state) ||
         (APP_NETWORK_MQTT_WAIT_PINGRESP == g_app_network.state)) &&
        network_time_reached(now_ms, s_deadline_ms))
    {
        ++g_app_network.timeout_count;
        network_backoff(APP_NETWORK_ERROR_TIMEOUT);
        return;
    }
    if (s_command_waiting || !network_time_reached(now_ms, s_next_action_ms))
    {
        return;
    }

    switch (g_app_network.state)
    {
        case APP_NETWORK_AT_SYNC:
            network_send_command("AT\r\n", NETWORK_COMMAND_TIMEOUT_MS);
            break;
        case APP_NETWORK_ECHO_OFF:
            network_send_command("ATE0\r\n", NETWORK_COMMAND_TIMEOUT_MS);
            break;
        case APP_NETWORK_STATION_MODE:
            network_send_command("AT+CWMODE_CUR=1\r\n", NETWORK_COMMAND_TIMEOUT_MS);
            break;
        case APP_NETWORK_SINGLE_CONNECTION:
            network_send_command("AT+CIPMUX=0\r\n", NETWORK_COMMAND_TIMEOUT_MS);
            break;
        case APP_NETWORK_DISABLE_AUTO_CONNECT:
            network_send_command("AT+CWAUTOCONN=0\r\n", NETWORK_COMMAND_TIMEOUT_MS);
            break;
        case APP_NETWORK_DISCONNECT_AP:
            network_send_command("AT+CWQAP\r\n", NETWORK_COMMAND_TIMEOUT_MS);
            break;
        case APP_NETWORK_JOIN_AP:
        {
            int const length = snprintf(s_command, sizeof(s_command),
                                        "AT+CWJAP_CUR=\"%s\",\"%s\"\r\n",
                                        APP_NETWORK_WIFI_SSID,
                                        APP_NETWORK_WIFI_PASSWORD);
            if ((length <= 0) || ((size_t) length >= sizeof(s_command)))
            {
                network_backoff(APP_NETWORK_ERROR_CONFIG);
            }
            else
            {
                network_send_command(s_command, NETWORK_JOIN_TIMEOUT_MS);
            }
            break;
        }
        case APP_NETWORK_TCP_CONNECT:
        {
            int const length = snprintf(s_command, sizeof(s_command),
                                        "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                                        APP_NETWORK_MQTT_HOST,
                                        (unsigned) APP_NETWORK_MQTT_PORT);
            if ((length <= 0) || ((size_t) length >= sizeof(s_command)))
            {
                network_backoff(APP_NETWORK_ERROR_CONFIG);
            }
            else
            {
                network_send_command(s_command, NETWORK_TCP_TIMEOUT_MS);
            }
            break;
        }
        case APP_NETWORK_MQTT_SEND_CONNECT:
        case APP_NETWORK_MQTT_SEND_SUBSCRIBE:
        case APP_NETWORK_MQTT_SEND_PUBLISH:
        case APP_NETWORK_MQTT_SEND_PING:
            network_start_packet_send();
            break;
        case APP_NETWORK_ONLINE:
            if (network_time_reached(now_ms, s_next_publish_ms))
            {
                s_next_publish_ms = now_ms + APP_NETWORK_TELEMETRY_INTERVAL_MS;
                if (network_format_telemetry() > 0)
                {
                    network_set_state(APP_NETWORK_MQTT_SEND_PUBLISH);
                }
            }
            else if ((uint32_t) (now_ms - s_last_tx_ms) >= NETWORK_PING_INTERVAL_MS)
            {
                network_set_state(APP_NETWORK_MQTT_SEND_PING);
            }
            break;
        default:
            break;
    }
}
