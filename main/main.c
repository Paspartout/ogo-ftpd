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
#include "ftp_server.h"

#include "event.h"
#include "graphics.h"
#include "tf.h"
#include "OpenSans_Regular_11X12.h"

#define WIFI_CONFIG_PATH "/sdcard/wifi.json"
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef VERSION
#define VERSION "0.0.0"
#endif

#ifndef APP_NAME
#define APP_NAME "ogo-ftpd"
#endif

// Notify task of wifi change
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
char* wifi_state_str(wifi_state_t state) {
	switch (state) {
		case WIFI_STATE_DISABLED:
			return "DISABLED";
			break;
		case WIFI_STATE_SCANNING:
			return "SCANNING";
			break;
		case WIFI_STATE_CONNECTING:
			return "CONNECTING";
			break;
		case WIFI_STATE_CONNECTED:
			return "CONNECTED";
			break;
		default:
			return "UNKNOWN";
			break;
	}
}


static tf_t *ui_font;
#define FONT_HEIGHT 12

static void ui_init() {
    ui_font = tf_new(&font_OpenSans_Regular_11X12, 0xFFFF, 0, TF_ALIGN_CENTER);

	// draw title
	const char *title = APP_NAME " " VERSION;
	tf_metrics_t m = tf_get_str_metrics(ui_font, title);
	point_t text_location = {
		.x = fb->width/2 - m.width/2,
		.y = 0,
	};
	tf_draw_str(fb, ui_font, title, text_location);
	display_update();
}

static void ui_free() {
	free(ui_font);
}

static void ui_display_text_centered(int y, const char *text) {
	tf_metrics_t m = tf_get_str_metrics(ui_font, text);
	// determine message location
	point_t text_location = {
		.x = fb->width/2 - m.width/2,
		.y = y,
	};
	rect_t clear_rec = {
		.x = 0,
		.y = text_location.y,
		.width = fb->width,
		.height = m.height,
	};
	fill_rectangle(fb, clear_rec, 0);
	tf_draw_str(fb, ui_font, text, text_location);
	display_update();
}

// Update the status display
static void ui_display_status() {
	char status[48];
	ip4_addr_t ip = wifi_get_ip();
	snprintf(status, sizeof(status), "WIFI %s, IP:" IPSTR, wifi_state_str(wifi_get_state()), IP2STR(&ip));
	ui_display_text_centered(100, status);
	display_update();
}

const char *event_names[] = {
    "ServerStarted", "ServerStopped", "ClientConnected", "ClientDisconnected", "Error",
};

static void ui_display_ftp_status(event_ftp_t ev) {
	char msg[128];
	if (ev.details == NULL) {
		snprintf(msg, sizeof(msg), "%s", event_names[ev.ftp_event]);
	} else {
		snprintf(msg, sizeof(msg), "%s: %s", event_names[ev.ftp_event], ev.details);
	}
	ui_display_text_centered(100+FONT_HEIGHT, msg);
}

const char *connect_prompt = "You now can connect to the ip address with port 21.";
const char *help_msg0 = "MENU: Back to firmware | START: Restart app.";
const char *help_msg1 = "If you can't connect restart might help :/";

// Display some help
static void ui_display_help() {
	ui_display_text_centered(fb->height-2*FONT_HEIGHT, help_msg0);
	ui_display_text_centered(fb->height-FONT_HEIGHT, help_msg1);

	display_update();
}

static void ui_display_msg(const char *title, const char *msg) {
	tf_metrics_t m1 = tf_get_str_metrics(ui_font, title);
	tf_metrics_t m2 = tf_get_str_metrics(ui_font, msg);
	// determine message location
	point_t text_location = {
		.x = fb->width/2 - (MAX(m1.width,m2.width))/2,
		.y = fb->height/2 - (m1.height+m2.height)/2,
	};
	rect_t clear_rec = {
		.x = 0,
		.y = text_location.y,
		.width = fb->width,
		.height = m1.height+m2.height,
	};

	fill_rectangle(fb, clear_rec, 0);
	tf_draw_str(fb, ui_font, title, text_location);
	text_location.y += m1.height;
	tf_draw_str(fb, ui_font, msg, text_location);

	display_update();
}

void restart() {
	ftp_stop();
	display_clear(0);
	display_drain();
	wifi_disable();
	sdcard_deinit();
	ui_free();
	esp_restart();
}

// This task updates the status of the wifi and ftp server and reacts to user input
static void main_task(void *arg) {
    event_t event;
	bool running = true;
	BaseType_t got_event;
	ui_display_status();
	ui_display_help();

	while(running) {
		// Handle events
        got_event = xQueueReceive(event_queue, &event, portMAX_DELAY);
		if (got_event != pdTRUE) {
			continue;
		}
		switch(event.type) {
			case EVENT_TYPE_KEYPAD:
				if (event.keypad.pressed & KEYPAD_MENU) {
					running = false;
				} else if (event.keypad.pressed & KEYPAD_START) {
					restart();
				}
				break;
			case EVENT_TYPE_WIFI_DISCONNECTED:
				ui_display_text_centered(100+FONT_HEIGHT*2, ""); // clears the line
				ui_display_status();
				ftp_stop();
				ftp_init();
				break;
			case EVENT_TYPE_WIFI_CONNECTED:
				ui_display_status();
				break;
			case EVENT_TYPE_WIFI_GOT_IP:
				ui_display_text_centered(100+FONT_HEIGHT*2, connect_prompt);
				ui_display_status();
				ftp_start();
				break;
			case EVENT_TYPE_FTP_EVENT:
				ui_display_ftp_status(event.ftp);
				break;
			default:
				printf("other event detected: %d\n", event.type);
				break;
		}
	}

	// Cleanup
	ftp_stop();
	display_clear(0);
	wifi_disable();
	sdcard_deinit();
	ui_free();

	// Return to menu/firmware after exit
    const esp_partition_t *part;
	part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
	// If no factory partition found, use first ota one
	if (part == NULL)
		part = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);

    esp_ota_set_boot_partition(part);
    esp_restart();
}

void app_main(void)
{
	display_init();
	backlight_init();
	backlight_percentage_set(50);
	keypad_init();
	event_init();
	ui_init();
	esp_err_t err;
	ESP_ERROR_CHECK(nvs_flash_init());

	if((err = sdcard_init("/sdcard")) != ESP_OK) {
		ui_display_msg("SDCARD ERROR!", "Please insert the sdcard and restart the device.");
		return;
	}

	wifi_init();
	if (wifi_load_config(WIFI_CONFIG_PATH) == -1) {
		ui_display_msg("CONFIGURATION ERROR!", "Make sure to have a proper wifi.json in your sdcards root.");
		return;
	}

	wifi_enable();
	wifi_register_changed_callback(on_wifi_changed);
	ftp_init();

    xTaskCreate(main_task, "main_task", 8192, NULL, 5, NULL);
}

