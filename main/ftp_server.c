#include <stdio.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

#include <esp_log.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ftp_server.h"

#include <uftpd.h>

TaskHandle_t ftp_task_handle;
uftpd_ctx ctx;
bool restarting = true;

static void ftp_task(void *arg) {
	// Wait for notification to start
	ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	printf("starting stuff!");
	while(restarting) {
		puts("starting server!");
		uftpd_start(&ctx);
	}
	puts("stopping server!");
	vTaskDelete(NULL);
}

void ftp_restart(void) {
	uftpd_stop(&ctx);
}

void ftp_stop(void) {
	restarting = false;
	uftpd_stop(&ctx);
}

void ftp_start(void) {
	xTaskNotifyGive(ftp_task_handle);
}

void notify_user(uftpd_event ev, const char *details) {
	// TODO: Send to user interface task
	printf("got event: %d\n", ev);
}

void ftp_init(void) {
	uftpd_init_localhost(&ctx, "21");
	uftpd_set_start_dir(&ctx, "/sdcard");
	uftpd_set_ev_callback(&ctx, notify_user);
    xTaskCreate(ftp_task, "ftp server", 65536, NULL, 5, &ftp_task_handle);
}

