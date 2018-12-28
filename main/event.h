#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <uftpd.h>

typedef enum {
	// Event to react to keypad presses
    EVENT_TYPE_KEYPAD,
	// Event to react to wifi connection
    EVENT_TYPE_WIFI_CONNECTED,
    EVENT_TYPE_WIFI_DISCONNECTED,
    EVENT_TYPE_WIFI_GOT_IP,
	// ftp event
    EVENT_TYPE_FTP_EVENT,
} event_type_t;

typedef struct {
    event_type_t type;
} event_head_t;

typedef struct {
    event_head_t head;
    uint16_t state;
    uint16_t pressed;
    uint16_t released;
} event_keypad_t;

typedef struct {
    event_head_t head;
	uftpd_event ftp_event;
	const char *details;
} event_ftp_t;

typedef union {
    event_type_t type;
    event_keypad_t keypad;
    event_ftp_t ftp;
} event_t;

extern QueueHandle_t event_queue;

void event_init(void);
