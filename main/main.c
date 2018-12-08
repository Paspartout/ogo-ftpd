#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include "backlight.h"
#include "display.h"
#include "keypad.h"
#include "sdcard.h"
#include "wifi.h"
#include "esp_spiffs.h"

#include "event.h"
#include "graphics.h"
#include "tf.h"
#include "OpenSans_Regular_11X12.h"

static void display_status_task(void *arg);
static void on_wifi_changed(system_event_id_t id);

void app_main(void)
{
    display_init();
    backlight_init();
    keypad_init();
    event_init();
    ESP_ERROR_CHECK(nvs_flash_init());
    sdcard_init("/sdcard");
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
	wifi_init();
	//wifi_restore_config();
	wifi_enable(); // TODO: FIx wifi configuration

	wifi_register_changed_callback(on_wifi_changed);

    const esp_partition_t *partition = esp_ota_get_running_partition();
	printf("booted from partition %s\n", partition->label);
	fflush(stdout);

    xTaskCreate(display_status_task, "display_status", 8192, NULL, 5, NULL);
}

// Notify display and
void on_wifi_changed(system_event_id_t id) {
    event_t event;
	switch(id) {
        case SYSTEM_EVENT_STA_CONNECTED:
			event.type = EVENT_TYPE_WIFI_CONNECTED;
			break;
        case SYSTEM_EVENT_STA_GOT_IP:
			event.type = EVENT_TYPE_WIFI_GOT_IP;
			break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
			event.type = EVENT_TYPE_WIFI_DISCONNECTED;
			break;
		default:
			break;
	}
    xQueueSend(event_queue, &event, 0);
}


// set dest pointer to description of state
void state_str(char** dest, wifi_state_t state) {
	switch (state) {
		case WIFI_STATE_DISABLED:
			*dest = "DISABLED";
			break;
		case WIFI_STATE_SCANNING:
			*dest = "SCANNING";
			break;
		case WIFI_STATE_CONNECTING:
			*dest = "CONNECTING";
			break;
		case WIFI_STATE_CONNECTED:
			*dest = "CONNECTED";
			break;
		default:
			*dest = "UNKNOWN";
			break;
	}
}

static tf_t *tf;
static char *wifi_state_str;

void view_init() {
    tf = tf_new(&font_OpenSans_Regular_11X12, 0xFFFF, 0, TF_ALIGN_CENTER);
}

void view_update() {
	char s[48];
	wifi_state_t state = wifi_get_state();
	ip4_addr_t ip = wifi_get_ip();
	state_str(&wifi_state_str, state);
	snprintf(s, sizeof(s), "State %s, IP:" IPSTR, wifi_state_str, IP2STR(&ip));
	tf_metrics_t m = tf_get_str_metrics(tf, s);

	// determine text location
	point_t text_location = {
		.x = fb->width/2 - m.width/2,
		.y = fb->height/2 - m.height/2,
	};
	rect_t clear_rec = {
		.x = 0,
		.y = text_location.y,
		.width = fb->width,
		.height = m.height,
	};

	// clear old text
	fill_rectangle(fb, clear_rec, 0);
	// draw new text
	tf_draw_str(fb, tf, s, text_location);
	display_update();
}

// This task displays the status of the wifi and ftp server
static void display_status_task(void *arg) {
    event_t event;
	bool running = true;
	BaseType_t got_event;
	view_init();
	view_update();

	while(running) {
		// Manual wifi connection request
		// TODO: Figure out why scanning doesn't work for my wifi
		wifi_state_t state = wifi_get_state();
		if (wifi_network_count > 0 && state != WIFI_STATE_CONNECTED && state != WIFI_STATE_CONNECTING) {
			printf("attempting to connect to first network\n");
			wifi_connect_network(wifi_networks[0]);
		}

		// Handle events
        got_event = xQueueReceive(event_queue, &event, portMAX_DELAY);
		if (got_event != pdTRUE) {
			continue;
		}
		switch(event.type) {
			case EVENT_TYPE_KEYPAD:
				if (event.keypad.pressed & KEYPAD_MENU) {
					running = false;
				} else if (event.keypad.pressed) {
					printf("another keypress detected: %d\n", event.keypad.pressed);
					// TODO: Handle things like start, stop server
				}
				break;
			case EVENT_TYPE_WIFI_DISCONNECTED:
				// TODO: Notify ftp server task
				view_update();
				printf("notifying ftp server\n");
				break;
			case EVENT_TYPE_WIFI_CONNECTED:
				view_update();
				break;
			case EVENT_TYPE_WIFI_GOT_IP:
				// TODO: Notify ftp server task
				view_update();
				printf("notifying ftp server\n");
				break;
			default:
				printf("other event detected: %d\n", event.type);
				break;
		}
	}

	// Cleanup
	display_clear(0);
	wifi_disable();
	sdcard_deinit();
	// Return to menu/firmware after exit
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    esp_ota_set_boot_partition(part);
    esp_restart();
}

