// main/monitoring.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Task IDs for monitoring
typedef enum {
    MON_TASK_PHYSICS = 0,
    MON_TASK_FLOCKING,
    MON_TASK_RADIO,
    MON_TASK_MQTT,
    MON_TASK_MAX
} MonTaskId;

// Initialization
void monitor_init(void);

// Call this at the VERY START of the task loop (after vTaskDelay)
void monitor_task_start(MonTaskId id);

// Call this at the VERY END of the task loop (before vTaskDelay loops back)
void monitor_task_end(MonTaskId id);

// Call this when a valid neighbour packet is received to track loss/availability
void monitor_report_packet(uint16_t seq_number, uint8_t *node_id);

// Call this to track radio energy states
void monitor_radio_state(bool is_tx, uint32_t duration_ms);

#ifdef __cplusplus
}
#endif