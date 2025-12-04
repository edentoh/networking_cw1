// main/globals.c
#include "tasks.h"
#include "config.h"

#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stdio.h>

static uint8_t MAC_ADDRESS[6];
static char    MAC_ADDRESS_STRING[18];

// Queues
#define CONTROL_INPUT_QUEUE_LENGTH   1
#define NEIGHBOUR_UPDATE_QUEUE_LENGTH  8
#define FLOCKING_STATE_QUEUE_LENGTH  1
#define RADIO_STATE_QUEUE_LENGTH     1
#define TELEMETRY_STATE_QUEUE_LENGTH 1

static QueueHandle_t ATTACK_QUEUE = NULL;
static QueueHandle_t CONTROL_INPUT_QUEUE      = NULL;
static QueueHandle_t NEIGHBOUR_UPDATE_QUEUE   = NULL;
static QueueHandle_t FLOCKING_STATE_QUEUE     = NULL;
static QueueHandle_t RADIO_STATE_QUEUE        = NULL;
static QueueHandle_t TELEMETRY_STATE_QUEUE    = NULL;

QueueHandle_t get_attack_queue(void) { return ATTACK_QUEUE; }
QueueHandle_t get_control_input_queue(void)   { return CONTROL_INPUT_QUEUE; }
QueueHandle_t get_neighbour_update_queue(void){ return NEIGHBOUR_UPDATE_QUEUE; }
QueueHandle_t get_flocking_state_queue(void)  { return FLOCKING_STATE_QUEUE; }
QueueHandle_t get_radio_state_queue(void)     { return RADIO_STATE_QUEUE; }
QueueHandle_t get_telemetry_state_queue(void) { return TELEMETRY_STATE_QUEUE; }

uint8_t *get_mac_address(void)                { return MAC_ADDRESS; }
const char *get_mac_address_string(void)      { return MAC_ADDRESS_STRING; }

void init_globals(void)
{
    // MAC
    esp_err_t err = esp_read_mac(MAC_ADDRESS, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        fast_log("GLOBALS (F): esp_read_mac failed: %d", err);
        vTaskDelay(portMAX_DELAY);
    }

    snprintf(MAC_ADDRESS_STRING, sizeof(MAC_ADDRESS_STRING),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             MAC_ADDRESS[0], MAC_ADDRESS[1], MAC_ADDRESS[2],
             MAC_ADDRESS[3], MAC_ADDRESS[4], MAC_ADDRESS[5]);

    // *** NEW: print MAC + room/world size ***
    fast_log("INIT (I): MAC address: %s", MAC_ADDRESS_STRING);
    fast_log("INIT (I): World bounds X:[%.1f, %.1f] Y:[%.1f, %.1f] Z:[%.1f, %.1f]",
             WORLD_MIN_X_MM, WORLD_MAX_X_MM,
             WORLD_MIN_Y_MM, WORLD_MAX_Y_MM,
             WORLD_MIN_Z_MM, WORLD_MAX_Z_MM);
    // Queues
    CONTROL_INPUT_QUEUE = xQueueCreate(CONTROL_INPUT_QUEUE_LENGTH,
                                       sizeof(ControlInput));
    NEIGHBOUR_UPDATE_QUEUE = xQueueCreate(NEIGHBOUR_UPDATE_QUEUE_LENGTH,
                                          sizeof(NeighbourState));
    FLOCKING_STATE_QUEUE = xQueueCreate(FLOCKING_STATE_QUEUE_LENGTH,
                                        sizeof(DroneState));
    RADIO_STATE_QUEUE = xQueueCreate(RADIO_STATE_QUEUE_LENGTH,
                                     sizeof(DroneState));
    TELEMETRY_STATE_QUEUE = xQueueCreate(TELEMETRY_STATE_QUEUE_LENGTH,
                                         sizeof(DroneState));
    // Create Attack Queue (Length 10 to buffer floods)
    ATTACK_QUEUE = xQueueCreate(10, sizeof(NeighbourState));

    if (!CONTROL_INPUT_QUEUE || !NEIGHBOUR_UPDATE_QUEUE ||
        !FLOCKING_STATE_QUEUE || !RADIO_STATE_QUEUE ||
        !TELEMETRY_STATE_QUEUE) {
        fast_log("GLOBALS (F): failed to create queues");
        vTaskDelay(portMAX_DELAY);
    }
}
