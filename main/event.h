#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
	// Event to react to keypad presses
    EVENT_TYPE_KEYPAD,
	// Event to react to wifi connection
    EVENT_TYPE_WIFI_CONNECTED,
    EVENT_TYPE_WIFI_DISCONNECTED,
    EVENT_TYPE_WIFI_GOT_IP,
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

typedef union {
    event_type_t type;
    event_keypad_t keypad;
} event_t;

extern QueueHandle_t event_queue;

void event_init(void);
