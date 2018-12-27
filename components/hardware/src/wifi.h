#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "esp_wifi.h"

typedef struct wifi_network_t {
    char ssid[33];
    char password[65];
    wifi_auth_mode_t authmode;
} wifi_network_t;

typedef enum wifi_state_t {
    WIFI_STATE_DISABLED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
} wifi_state_t;

typedef void (*wifi_scan_done_cb_t)(void *arg);
typedef void (*wifi_changed_cb_t)(system_event_id_t);

wifi_network_t **wifi_networks;
size_t wifi_network_count;

void wifi_init(void);
void wifi_enable(void);
void wifi_disable(void);
void wifi_connect_network(wifi_network_t *network);
size_t wifi_network_add(wifi_network_t *network);
int wifi_network_delete(wifi_network_t *network);
wifi_state_t wifi_get_state(void);
ip4_addr_t wifi_get_ip(void);
void wifi_register_scan_done_callback(wifi_scan_done_cb_t cb, void *arg);
void wifi_register_changed_callback(wifi_changed_cb_t cb);
void wifi_backup_config(void);
int wifi_restore_config(void);
int wifi_load_config(void);
