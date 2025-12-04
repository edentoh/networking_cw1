// main/main.cpp
extern "C" {
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "tasks.h"
}

extern "C" void app_main(void)
{
    logger_init();
    fast_log("MAIN (I): starting up");

    init_globals();

    if (wifi_connect() != ESP_OK) {
        fast_log("MAIN (F): Wi-Fi connect failed, continuing without network");
    }

    if (sync_time() != ESP_OK) {
        fast_log("MAIN (W): time sync failed, continuing with unsynchronised time");
        // carry on anyway – timestamps will just be relative
    }

    init_flocking();
    init_physics();
    init_radio();
    // enable if you need MQTT
    init_mqtt_telemetry();

    fast_log("MAIN (I): all tasks started");
}
