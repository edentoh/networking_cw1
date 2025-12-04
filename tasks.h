// main/tasks.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "drone_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------- Logging ----------
void logger_init(void);
void fast_log(const char *fmt, ...);

// ---------- Wi-Fi + time ----------
esp_err_t wifi_connect(void);
esp_err_t sync_time(void);
void get_current_unix_time(uint32_t *ts_s, uint16_t *ts_ms);

// ---------- Globals (queues + MAC) ----------
void init_globals(void);

QueueHandle_t get_control_input_queue(void);
QueueHandle_t get_neighbour_update_queue(void);
QueueHandle_t get_flocking_state_queue(void);
QueueHandle_t get_radio_state_queue(void);
QueueHandle_t get_telemetry_state_queue(void);

uint8_t *get_mac_address(void);
const char *get_mac_address_string(void);

// ---------- Security (AES-CMAC + Logic) ----------
void sign_packet(NeighbourState *state);
bool verify_packet(NeighbourState *state);
bool security_validate_packet(const NeighbourState *n);

// ---------- Tasks (subsystems) ----------
void init_physics(void);
void init_flocking(void);
void init_radio(void);
void init_mqtt_telemetry(void);

QueueHandle_t get_attack_queue(void);
void init_attacker(void);

#ifdef __cplusplus
}
#endif