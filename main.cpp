// main/main.cpp
extern "C" {
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "tasks.h"
}

// Ensure init_attacker is declared if tasks.h doesn't have it yet
extern "C" void init_attacker(void);

extern "C" void app_main(void)
{
    logger_init();
    fast_log("MAIN (I): starting up");

    init_globals();

    if (wifi_connect() != ESP_OK) {
        fast_log("MAIN (F): Wi-Fi connect failed, continuing without network");
    }

    if (sync_time() != ESP_OK) {
        fast_log("MAIN (W): time sync failed, continuing");
    }

    init_flocking();
    init_physics();
    init_radio();
    init_mqtt_telemetry();

    // Attack task conditional init
#if ENABLE_ATTACK_TASK
    fast_log("MAIN (W): !!! ATTACK MODE ENABLED !!!");
    init_attacker();
#else
    fast_log("MAIN (I): Attack mode disabled (Normal Operation)");
#endif
    // -----------------------------

    fast_log("MAIN (I): all tasks started");
}