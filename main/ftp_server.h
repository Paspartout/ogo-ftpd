#pragma once

#include "freertos/FreeRTOS.h"

extern TaskHandle_t ftp_task_handle;

void ftp_init(void);
void ftp_stop(void);
void ftp_start(void);
void ftp_restart(void);
