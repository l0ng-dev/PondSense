#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <stdint.h>

typedef enum
{
    APP_NETWORK_DISABLED = 0,
    APP_NETWORK_AT_SYNC,
    APP_NETWORK_ECHO_OFF,
    APP_NETWORK_STATION_MODE,
    APP_NETWORK_SINGLE_CONNECTION,
    APP_NETWORK_DISABLE_AUTO_CONNECT,
    APP_NETWORK_DISCONNECT_AP,
    APP_NETWORK_JOIN_AP,
    APP_NETWORK_TCP_CONNECT,
    APP_NETWORK_MQTT_SEND_CONNECT,
    APP_NETWORK_MQTT_WAIT_CONNACK,
    APP_NETWORK_MQTT_SEND_SUBSCRIBE,
    APP_NETWORK_MQTT_WAIT_SUBACK,
    APP_NETWORK_ONLINE,
    APP_NETWORK_MQTT_SEND_PUBLISH,
    APP_NETWORK_MQTT_SEND_PING,
    APP_NETWORK_MQTT_WAIT_PINGRESP,
    APP_NETWORK_BACKOFF
} app_network_state_t;

typedef enum
{
    APP_NETWORK_ERROR_NONE = 0,
    APP_NETWORK_ERROR_CONFIG,
    APP_NETWORK_ERROR_UART,
    APP_NETWORK_ERROR_RESPONSE,
    APP_NETWORK_ERROR_TIMEOUT,
    APP_NETWORK_ERROR_RX_OVERFLOW,
    APP_NETWORK_ERROR_MQTT_PACKET,
    APP_NETWORK_ERROR_MQTT_CONNACK,
    APP_NETWORK_ERROR_MQTT_SUBACK
} app_network_error_t;

/* 不包含密码和 MQTT Client ID，可直接在 Keil Watch 查看。 */
typedef struct
{
    app_network_state_t state;
    app_network_error_t last_error;
    app_network_state_t last_failed_state;
    uint32_t at_send_count;
    uint32_t wifi_join_count;
    uint32_t mqtt_connect_count;
    uint32_t mqtt_subscribe_count;
    uint32_t mqtt_publish_sent_count;
    uint32_t mqtt_rx_publish_count;
    uint32_t reconnect_count;
    uint32_t timeout_count;
    uint32_t rx_overflow_count;
    uint32_t mqtt_oversize_publish_count;
    uint32_t last_online_ms;
    uint8_t mqtt_connack_code;
    uint8_t mqtt_suback_code;
    char last_downlink[96];
} app_network_status_t;

extern app_network_status_t g_app_network;

void App_Network_Init(uint32_t now_ms);
void App_Network_Process(uint32_t now_ms);

#endif
