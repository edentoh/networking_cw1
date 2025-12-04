// main/sntp_time.c
#include "tasks.h"

#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys/time.h"
#include <stdlib.h>

esp_err_t sync_time(void)
{
    fast_log("SNTP (I): init...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "europe.pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "GMT0", 1);
    tzset();

    int retries = 0;
    const int max_retries = 20;

    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED &&
           retries < max_retries) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        retries++;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        fast_log("SNTP (I): time synced");
        return ESP_OK;
    }

    fast_log("SNTP (E): failed to sync time after %d retries, using local time",
             max_retries);
    // IMPORTANT: still return ESP_OK so the rest of the system can run
    return ESP_OK;
}

void get_current_unix_time(uint32_t* ts_s, uint16_t* ts_ms)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        *ts_s  = tv.tv_sec;
        *ts_ms = tv.tv_usec / 1000;
    } else {
        fast_log("TIME (E): gettimeofday failed");
        *ts_s = 0;
        *ts_ms = 0;
    }
}
