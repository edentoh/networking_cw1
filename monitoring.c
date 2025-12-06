// main/monitoring.c
#include "monitoring.h"
#include "config.h"
#include "tasks.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h> // abs 

// Thresholds
#define CHANGE_ALERT_THRESHOLD   1.5f // 50% increase triggers alert
#define EMA_ALPHA                0.2f // 0.2 = moderate adaptation speed

typedef struct {
    const char* name;
    uint32_t    target_period_ms;
    
    TickType_t  last_start_tick;
    
    uint32_t    max_jitter_ms;
    uint32_t    samples;
    uint32_t    sum_jitter;
    uint32_t    sum_latency;

    float       moving_avg_jitter;
    float       moving_avg_latency;
} TaskStats;

static TaskStats TASKS[MON_TASK_MAX] = {
    { "PHYS", PHYSICS_PERIOD_MS, 0, 0, 0, 0, 0, 0.0f, 0.0f },
    { "FLOC", FLOCKING_PERIOD_MS, 0, 0, 0, 0, 0, 0.0f, 0.0f },
    { "RADI", RADIO_TX_PERIOD_MS, 0, 0, 0, 0, 0, 0.0f, 0.0f },
    { "MQTT", MQTT_TELEMETRY_PERIOD_MS, 0, 0, 0, 0, 0, 0.0f, 0.0f }
};

// Availability
static uint32_t total_packets_rx = 0;
static uint32_t total_packets_lost = 0;
static uint16_t last_seq_map[MAX_NEIGHBOURS];
static bool     node_seen[MAX_NEIGHBOURS];

// Energy Stats
static uint32_t energy_tx_time_ms = 0;
static uint64_t start_time_ms = 0;

// Energy Change Detection State
static uint32_t last_report_tx_time_ms = 0; // Snapshot of TX time at last report
static float    moving_avg_mah = 0.0f;      // Average energy per window

// -----------------------------------------------------------------------------
// TIMING HELPERS
// -----------------------------------------------------------------------------
void monitor_task_start(MonTaskId id)
{
    if (id >= MON_TASK_MAX) return;
    TaskStats *t = &TASKS[id];
    TickType_t now = xTaskGetTickCount();

    if (t->last_start_tick > 0) {
        uint32_t interval = pdTICKS_TO_MS(now - t->last_start_tick);
        int32_t diff = (int32_t)interval - (int32_t)t->target_period_ms;
        uint32_t jitter = abs(diff);

        if (jitter > t->max_jitter_ms) t->max_jitter_ms = jitter;
        t->sum_jitter += jitter;
    }
    t->last_start_tick = now;
}

void monitor_task_end(MonTaskId id)
{
    if (id >= MON_TASK_MAX) return;
    TaskStats *t = &TASKS[id];
    
    uint32_t dur = pdTICKS_TO_MS(xTaskGetTickCount() - t->last_start_tick);
    t->sum_latency += dur;
    t->samples++;
}

// -----------------------------------------------------------------------------
// AVAILABILITY & ENERGY HOOKS
// -----------------------------------------------------------------------------
void monitor_report_packet(uint16_t seq_number, uint8_t *node_id)
{
    uint8_t idx = node_id[5] % MAX_NEIGHBOURS;
    if (node_seen[idx]) {
        int16_t diff = seq_number - last_seq_map[idx];
        if (diff > 1) {
            total_packets_lost += (diff - 1);
        }
    }
    node_seen[idx] = true;
    last_seq_map[idx] = seq_number;
    total_packets_rx++;
}

void monitor_radio_state(bool is_tx, uint32_t duration_ms)
{
    if (is_tx) {
        energy_tx_time_ms += duration_ms;
    }
}

// -----------------------------------------------------------------------------
// REPORTING TASK
// -----------------------------------------------------------------------------
static void monitor_task_func(void *arg)
{
    (void)arg;
    start_time_ms = pdTICKS_TO_MS(xTaskGetTickCount());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_REPORT_PERIOD_MS));
        
        uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        uint32_t total_time_s = (now_ms - start_time_ms) / 1000;
        if (total_time_s == 0) total_time_s = 1;

        fast_log("--- PERFORMANCE REPORT (%us) ---", total_time_s);
        
        // 1. Timing & Anomaly Detection (Jitter/Latency)
        fast_log("TASK  | Jit(Avg/Base) | Lat(Avg/Base) | Alerts");
        for (int i=0; i<MON_TASK_MAX; ++i) {
            TaskStats *t = &TASKS[i];
            if (t->samples == 0) continue;
            
            float cur_jit = (float)t->sum_jitter / t->samples;
            float cur_lat = (float)t->sum_latency / t->samples;
            
            if (t->moving_avg_jitter == 0.0f) t->moving_avg_jitter = cur_jit;
            if (t->moving_avg_latency == 0.0f) t->moving_avg_latency = cur_lat;

            bool alert = false;
            // Check Spikes
            if (t->moving_avg_latency > 1.0f && cur_lat > t->moving_avg_latency * CHANGE_ALERT_THRESHOLD) alert = true;
            if (t->moving_avg_jitter > 1.0f && cur_jit > t->moving_avg_jitter * CHANGE_ALERT_THRESHOLD) alert = true;

            if (!alert) {
                fast_log("%-5s | %3.0f / %3.0f ms  | %3.0f / %3.0f ms  | OK", 
                         t->name, cur_jit, t->moving_avg_jitter, cur_lat, t->moving_avg_latency);
            } else {
                fast_log("%-5s | %3.0f / %3.0f ms  | %3.0f / %3.0f ms  | !! SPIKE !!", 
                         t->name, cur_jit, t->moving_avg_jitter, cur_lat, t->moving_avg_latency);
            }

            // Update Baselines
            t->moving_avg_jitter  = (EMA_ALPHA * cur_jit) + ((1.0f - EMA_ALPHA) * t->moving_avg_jitter);
            t->moving_avg_latency = (EMA_ALPHA * cur_lat) + ((1.0f - EMA_ALPHA) * t->moving_avg_latency);
            
            t->max_jitter_ms = 0; t->sum_jitter = 0; t->sum_latency = 0; t->samples = 0;
        }

        // 2. Availability
        uint32_t total = total_packets_rx + total_packets_lost;
        float avail_pct = (total > 0) ? (100.0f * total_packets_rx / total) : 0.0f;
        fast_log("NET   | RX: %lu | Lost: %lu | Avail: %.1f%%", 
                 total_packets_rx, total_packets_lost, avail_pct);

        // 3. Energy Change Detection
        // Calculate usage ONLY for this window (delta)
        uint32_t current_tx_total = energy_tx_time_ms;
        uint32_t delta_tx_ms = current_tx_total - last_report_tx_time_ms;
        uint32_t delta_window_ms = MONITOR_REPORT_PERIOD_MS;
        uint32_t delta_rx_ms = delta_window_ms - delta_tx_ms;

        // Calculate mAh for this specific 5-second window
        double window_mah = 0;
        window_mah += (EST_CURRENT_BASE_MA * (delta_window_ms / 1000.0f));
        window_mah += (EST_CURRENT_LORA_RX_MA * (delta_rx_ms / 1000.0f));
        window_mah += (EST_CURRENT_LORA_TX_MA * (delta_tx_ms / 1000.0f));
        window_mah /= 3600.0; // Convert to mAh

        // Init baseline
        if (moving_avg_mah == 0.0f) moving_avg_mah = (float)window_mah;

        // ---------------------------------------------------------
        // UPDATED: Calculate % Change and Print Numeric Alert
        // ---------------------------------------------------------
        float change_pct = 0.0f;
        if (moving_avg_mah > 0.00001f) {
            change_pct = ((window_mah - moving_avg_mah) / moving_avg_mah) * 100.0f;
        }

        // Check for Deviation
        if (window_mah > moving_avg_mah * CHANGE_ALERT_THRESHOLD) {
            fast_log("PWR   | !! +%.0f%% CHANGE !! | Cur: %.4f mAh | Base: %.4f", 
                     change_pct, window_mah, moving_avg_mah);
        } else {
            fast_log("PWR   | Normal (%+.0f%%)    | Cur: %.4f mAh | Base: %.4f", 
                     change_pct, window_mah, moving_avg_mah);
        }

        // Update State
        moving_avg_mah = (EMA_ALPHA * window_mah) + ((1.0f - EMA_ALPHA) * moving_avg_mah);
        last_report_tx_time_ms = current_tx_total;

        fast_log("--------------------------------");
    }
}

void monitor_init(void)
{
    memset(node_seen, 0, sizeof(node_seen));
    xTaskCreate(monitor_task_func, "monitor", 4096, NULL, 1, NULL);
}