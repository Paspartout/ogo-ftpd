#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"

#include "frozen.h"
#include "wifi.h"


#define CONFIG_FILE "/spiffs/wifi.json"
#define BACKUP_CONFIG_FILE "/sdcard/wifi.json"

wifi_network_t **wifi_networks = NULL;
size_t wifi_network_count = 0;

static volatile wifi_state_t s_wifi_state = WIFI_STATE_DISABLED;
static bool s_ignore_disconnect = false;
static wifi_network_t *s_current_network = NULL;
ip4_addr_t s_wifi_ip = { 0 };
static wifi_scan_done_cb_t s_scan_done_cb = NULL;
static void *s_scan_done_arg = NULL;
static wifi_changed_cb_t s_changed_cb = NULL;

static wifi_ap_record_t *s_scan_results = NULL;
static uint16_t s_scan_result_count = 0;
static uint16_t s_scan_index = 0;


static void start_scan(void)
{
    wifi_scan_config_t config = { 0 };
    esp_wifi_scan_start(&config, false);
    s_wifi_state = WIFI_STATE_SCANNING;
}

static int compare_wifi_ap_record_rssi(const void *a, const void *b)
{
    const wifi_ap_record_t *aa = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *bb = (const wifi_ap_record_t *)b;

    int cmp = (aa->rssi < bb->rssi) - (aa->rssi > bb->rssi);
    if (cmp != 0) {
        return cmp;
    }

    return strcasecmp((const char *)aa->ssid, (const char *)bb->ssid);
    return cmp;
}

static void scan_connect(void)
{
    for (; s_scan_index < s_scan_result_count; s_scan_index++) {
        for (size_t i = 0; i < wifi_network_count; i++) {
            if (strcmp((const char *)s_scan_results[s_scan_index].ssid, wifi_networks[i]->ssid) == 0) {
                s_scan_index += 1; /* skip this network if connection fails */
                wifi_connect_network(wifi_networks[i]);
                return;
            }
        }
    }
    start_scan();
}

static void scan_done(void)
{
    if (s_scan_results) {
        free(s_scan_results);
        s_scan_results = NULL;
    }
    esp_wifi_scan_get_ap_num(&s_scan_result_count);
    s_scan_results = malloc(s_scan_result_count * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(&s_scan_result_count, s_scan_results);
    qsort(s_scan_results, s_scan_result_count, sizeof(wifi_ap_record_t), compare_wifi_ap_record_rssi);

    s_scan_index = 0;
    scan_connect();
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case SYSTEM_EVENT_STA_CONNECTED:
            s_wifi_state = WIFI_STATE_CONNECTED;
            s_ignore_disconnect = false;
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            s_wifi_ip = event->event_info.got_ip.ip_info.ip;
            break;

        case SYSTEM_EVENT_SCAN_DONE:
            if (s_wifi_state == WIFI_STATE_SCANNING) {
                scan_done();
                break;
            }
            if (s_scan_done_cb) {
                s_scan_done_cb(s_scan_done_arg);
            }
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            memset(&s_wifi_ip, 0, sizeof(s_wifi_ip));
            if (s_ignore_disconnect) {
                s_ignore_disconnect = false;
                break;
            }
            if (s_wifi_state == WIFI_STATE_CONNECTED) {
                s_wifi_state = WIFI_STATE_DISCONNECTED;
                esp_wifi_connect();
                break;
            }
            if (s_wifi_state != WIFI_STATE_DISABLED) {
                scan_connect();
            }
            break;

        default:
            break;
    }

	printf("system event: %d\n", event->event_id);
	// Notify user if connection changed
	if (s_changed_cb) {
		s_changed_cb(event->event_id);
	}

    return ESP_OK;
}

int wifi_load_config(const char *path)
{
    const char *data;
	
	// Remove loaded networks before reading new ones
    if (wifi_networks != NULL) {
        for (int i = 0; i < wifi_network_count; i++) {
            free(wifi_networks[i]);
        }
        free(wifi_networks);
        wifi_networks = NULL;
        wifi_network_count = 0;
    }

    data = json_fread(path);
    if (data == NULL) {
		perror("cant open wifi json");
        return -1;
    }

    struct json_token t;
    for (int i = 0; json_scanf_array_elem(data, strlen(data), ".networks", i, &t) > 0; i++) {
        char *ssid = NULL;
        char *password = NULL; 
        char *authmode = NULL;

		// TODO: Error checking? File contents could be malformed
        json_scanf(t.ptr, t.len, "{ssid: %Q, password: %Q, authmode: %Q}", &ssid, &password, &authmode);

        wifi_networks = realloc(wifi_networks, sizeof(wifi_network_t *) * (wifi_network_count + 1));
        assert(wifi_networks != NULL);
        wifi_network_t *network = calloc(1, sizeof(wifi_network_t));
        assert(network != NULL);

        if (ssid) {
            strncpy(network->ssid, ssid, sizeof(network->ssid));
            network->ssid[sizeof(network->ssid) - 1] = '\0';
            free(ssid);
        }

        if (password) {
            strncpy(network->password, password, sizeof(network->password));
            network->password[sizeof(network->password) - 1] = '\0';
            free(password);
        }

        if (authmode) {
            if (strcasecmp(authmode, "open") == 0) {
                network->authmode = WIFI_AUTH_OPEN;
            } else if (strcasecmp(authmode, "wep") == 0) {
                network->authmode = WIFI_AUTH_WEP;
            } else if (strcasecmp(authmode, "wpa-psk") == 0) {
                network->authmode = WIFI_AUTH_WPA_PSK;
            } else if (strcasecmp(authmode, "wpa2-psk") == 0) {
                network->authmode = WIFI_AUTH_WPA2_PSK;
            } else if (strcasecmp(authmode, "wpa/wpa2-psk") == 0) {
                network->authmode = WIFI_AUTH_WPA_WPA2_PSK;
            }
            free(authmode);
        }

        wifi_networks[wifi_network_count] = network;
        wifi_network_count += 1;
    }

	return 0;
}

static int json_printf_network(struct json_out *out, va_list *ap)
{
    wifi_network_t *network = va_arg(*ap, wifi_network_t *);
    char *authmode;
    switch (network->authmode) {
        case WIFI_AUTH_OPEN:
            authmode = "open";
            break;
        case WIFI_AUTH_WEP:
            authmode = "wep";
            break;
        case WIFI_AUTH_WPA_PSK:
            authmode = "wpa-psk";
            break;
        case WIFI_AUTH_WPA2_PSK:
            authmode = "wpa2-psk";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            authmode = "wpa/wpa2-psk";
            break;
        default:
            authmode = "unknown";
            break;
    }
    return json_printf(out, "{ssid: %Q, password: %Q, authmode: %Q}", network->ssid, network->password, authmode);
}

static int json_printf_networks(struct json_out *out, va_list *ap)
{
    int len = 0;
    wifi_network_t **networks = va_arg(*ap, wifi_network_t **);
    size_t network_count = va_arg(*ap, size_t);
    len += json_printf(out, "[");
    for (int i = 0; i < network_count; i++) {
        if (i > 0) {
            len += json_printf(out, ", ");
        }
        len += json_printf(out, "%M", json_printf_network, networks[i]);
    }
    len += json_printf(out, "]");
    return len;
}

static void write_config(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return; /* fail silently */
    }
    struct json_out out = JSON_OUT_FILE(f);
    json_printf(&out, "{networks: %M}", json_printf_networks, wifi_networks, wifi_network_count);
    fclose(f);
}

static int compare_wifi_networks(const void *a, const void *b)
{
    const wifi_network_t *aa = (const wifi_network_t *)a;
    const wifi_network_t *bb = (const wifi_network_t *)b;

    return strcasecmp((const char *)aa->ssid, (const char *)bb->ssid);
}

static int find_last_wifi_network(wifi_network_t *network)
{
    int i;
    for (i = 0; i < wifi_network_count; i++) {
        int n = compare_wifi_networks(network, wifi_networks[i]);
        if (n == 0) {
            int last_seen = i;
            i += 1;
            while (i < wifi_network_count) {
                n = compare_wifi_networks(network, wifi_networks[i]);
                if (n == 0) {
                    last_seen = i;
                } else {
                    break;
                }
                i += 1;
            }
            return last_seen;
        }
        if (n < 0) {
            break;
        }
    }
    return -(i + 1);
}

size_t wifi_network_add(wifi_network_t *network)
{
    int i = find_last_wifi_network(network);
    if (i >= 0) {
        i += 1; /* insert after last */
    } else {
        i = -(i + 1);
    }

    wifi_networks = realloc(wifi_networks, sizeof(wifi_network_t *) * (wifi_network_count + 1));
    assert(wifi_networks != NULL);
    memmove(&wifi_networks[i + 1], &wifi_networks[i], sizeof(wifi_network_t *) * (wifi_network_count - i));
    wifi_networks[i] = malloc(sizeof(wifi_network_t));
    assert(wifi_networks[i] != NULL);
    memcpy(wifi_networks[i], network, sizeof(wifi_network_t));
    wifi_network_count += 1;

    write_config(CONFIG_FILE ".new");
    remove(CONFIG_FILE);
    rename(CONFIG_FILE ".new", CONFIG_FILE);

    if (s_wifi_state != WIFI_STATE_DISABLED && s_wifi_state != WIFI_STATE_CONNECTED) {
        start_scan();
    }

    return i;
}

int wifi_network_delete(wifi_network_t *network)
{
    size_t i;
    for (i = 0; i < wifi_network_count; i++) {
        if (network == wifi_networks[i]) {
            break;
        }
    }
    if (i >= wifi_network_count) {
        return -1;
    }

    if (s_current_network == wifi_networks[i]) {
        if (s_wifi_state == WIFI_STATE_CONNECTED) {
            start_scan();
        }
    }

    if (i < wifi_network_count - 1) {
        memmove(&wifi_networks[i], &wifi_networks[i + 1], sizeof(wifi_network_t *) * (wifi_network_count - i - 1));
    }
    wifi_networks = realloc(wifi_networks, sizeof(wifi_network_t *) * wifi_network_count - 1);
    assert(wifi_networks != NULL);
    wifi_network_count -= 1;

    write_config(CONFIG_FILE ".new");
    remove(CONFIG_FILE);
    rename(CONFIG_FILE ".new", CONFIG_FILE);

    return i;
}

wifi_state_t wifi_get_state(void)
{
    return s_wifi_state;
}

ip4_addr_t wifi_get_ip(void)
{
    return s_wifi_ip;
}

void wifi_init(void)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
}

void wifi_enable(void)
{
    static bool wifi_init = false;

    if (s_wifi_state != WIFI_STATE_DISABLED) {
        return;
    }

    if (!wifi_init) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        cfg.nvs_enable = false;
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        wifi_init = true;
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    // UGH: This made my wifi not work reliably
    // ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_wifi_state = WIFI_STATE_DISCONNECTED;

    start_scan();
}

void wifi_disable(void)
{
    if (s_wifi_state == WIFI_STATE_DISABLED) {
        return;
    }

    if (s_wifi_state == WIFI_STATE_CONNECTED) {
        s_ignore_disconnect = true;
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        s_wifi_state = WIFI_STATE_DISCONNECTED;
    }

    ESP_ERROR_CHECK(esp_wifi_stop());
    s_wifi_state = WIFI_STATE_DISABLED;
}

void wifi_connect_network(wifi_network_t *network)
{
    if (s_wifi_state == WIFI_STATE_CONNECTED) {
        s_ignore_disconnect = true;
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        s_wifi_state = WIFI_STATE_DISCONNECTED;
    }

    if (network == NULL) {
        return;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .scan_method = WIFI_FAST_SCAN,
            .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
            .threshold.rssi = -127,
            .threshold.authmode = network->authmode,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, network->ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, network->password, sizeof(wifi_config.sta.password));

    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
    esp_wifi_connect();
    s_wifi_state = WIFI_STATE_CONNECTING;

    s_current_network = network;
}

void wifi_register_scan_done_callback(wifi_scan_done_cb_t cb, void *arg)
{
    s_scan_done_cb = cb;
    s_scan_done_arg = arg;
}

void wifi_register_changed_callback(wifi_changed_cb_t cb)
{
	s_changed_cb = cb;
}

void wifi_backup_config(void)
{
    write_config(BACKUP_CONFIG_FILE);
}

int wifi_restore_config(void)
{
    if (wifi_load_config(BACKUP_CONFIG_FILE) != 0) {
		return -1;
	}
    write_config(CONFIG_FILE ".new");
    remove(CONFIG_FILE);
    rename(CONFIG_FILE ".new", CONFIG_FILE);
	return 0;
}
