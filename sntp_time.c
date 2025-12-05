// main/sntp_time.c
#include "tasks.h"

#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"  // Required for Semaphore
#include "sys/time.h"
#include <stdlib.h>

// Global semaphore handle
static SemaphoreHandle_t SNTP_SEM = NULL;

// -----------------------------------------------------------------------------
// Callback: Triggered by the system when time is successfully updated
// -----------------------------------------------------------------------------
void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    // fast_log("SNTP (I): Notification received!"); // Optional debug
    if (SNTP_SEM) {
        xSemaphoreGive(SNTP_SEM); // Wake up the waiting task
    }
}

// -----------------------------------------------------------------------------
// Sync function using Semaphore
// -----------------------------------------------------------------------------
esp_err_t sync_time(void)
{
    fast_log("SNTP (I): init...");

    // 1. Create the binary semaphore (initially empty)
    SNTP_SEM = xSemaphoreCreateBinary();
    if (SNTP_SEM == NULL) {
        fast_log("SNTP (E): No memory for semaphore");
        return ESP_OK; // Continue anyway
    }

    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    // 2. Register the callback function
    // This tells the SNTP library: "Call this function when you get the time"
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // Configure Servers (IP strings to bypass DNS)
    esp_sntp_setservername(0, "216.239.35.0"); // Google
    esp_sntp_setservername(1, "162.159.200.1"); // Cloudflare

    esp_sntp_init();

    setenv("TZ", "GMT0", 1);
    tzset();

    fast_log("SNTP (W): waiting for sync (semaphore)...");

    // 3. Block and Wait
    // The task sleeps here. It will wake up ONLY if:
    // A) The callback gives the semaphore (Success)
    // B) 20 seconds pass (Timeout)
    if (xSemaphoreTake(SNTP_SEM, pdMS_TO_TICKS(20000)) == pdTRUE) {
        fast_log("SNTP (I): time synced, current time: %s", ctime((const time_t[]){time(NULL)}));
        return ESP_OK;
    }

    // 4. Handle Timeout
    // Check one last time in case the semaphore was missed (rare race condition)
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        fast_log("SNTP (I): time synced (status check)");
        return ESP_OK;
    }

    fast_log("SNTP (E): sync timeout, using local time");
    
    // Clean up semaphore (optional, but good practice if not used again)
    vSemaphoreDelete(SNTP_SEM);
    SNTP_SEM = NULL;

    return ESP_OK;
}

void get_current_unix_time(uint32_t* ts_s, uint16_t* ts_ms)
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        *ts_s  = tv.tv_sec;
        *ts_ms = tv.tv_usec / 1000;
    } else {
        *ts_s = 0;
        *ts_ms = 0;
    }
}