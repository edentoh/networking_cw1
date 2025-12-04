// main/physics.c
#include "tasks.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void physics_task(void *arg)
{
    (void)arg;

    QueueHandle_t control_q   = get_control_input_queue();
    QueueHandle_t flocking_q  = get_flocking_state_queue();
    QueueHandle_t radio_q     = get_radio_state_queue();
    QueueHandle_t telemetry_q = get_telemetry_state_queue();

    TickType_t period_ticks   = pdMS_TO_TICKS(PHYSICS_PERIOD_MS);
    TickType_t next_wake      = xTaskGetTickCount();

    // Start in middle of world
    DroneState s = {
        .x_mm = (WORLD_MIN_X_MM + WORLD_MAX_X_MM) / 3.0,
        .y_mm = (WORLD_MIN_Y_MM + WORLD_MAX_Y_MM) / 3.0,
        .z_mm = (WORLD_MIN_Z_MM + WORLD_MAX_Z_MM) / 3.0,
        .vx_mm_s = 0,
        .vy_mm_s = 0,
        .vz_mm_s = 0,
        .yaw_cd = 0,
        .yaw_rate_cd_s = 0
    };

    ControlInput u = {0};

    while (true) {
        vTaskDelayUntil(&next_wake, period_ticks);

// ... inside physics_task while(true) loop ...

        // New command?
        if (xQueueReceive(control_q, &u, 0) == pdTRUE) {
            // Don't set velocity directly. Store the target.
            // We can just use 'u' as the target reference.
        }

        double dt = PHYSICS_PERIOD_MS / 1000.0;
        
        // --- SMOOTHING / INERTIA ---
        // Alpha determines responsiveness. 
        // 0.1 = heavy/slow, 0.5 = snappy, 1.0 = instant (current behavior)
        const double alpha = 0.2; 

        s.vx_mm_s += (u.target_vx_mm_s - s.vx_mm_s) * alpha;
        s.vy_mm_s += (u.target_vy_mm_s - s.vy_mm_s) * alpha;
        s.vz_mm_s += (u.target_vz_mm_s - s.vz_mm_s) * alpha;
        
        // Yaw rate is usually snappy, but we can smooth it too if desired
        s.yaw_rate_cd_s += (u.target_yaw_rate_cd_s - s.yaw_rate_cd_s) * alpha;

        // Position Integration
        s.x_mm   += s.vx_mm_s * dt;
        s.y_mm   += s.vy_mm_s * dt;
        s.z_mm   += s.vz_mm_s * dt;
        s.yaw_cd += s.yaw_rate_cd_s * dt;

        // Clamp to world box
        if (s.x_mm < WORLD_MIN_X_MM) { s.x_mm = WORLD_MIN_X_MM; s.vx_mm_s = 0; }
        if (s.x_mm > WORLD_MAX_X_MM) { s.x_mm = WORLD_MAX_X_MM; s.vx_mm_s = 0; }
        if (s.y_mm < WORLD_MIN_Y_MM) { s.y_mm = WORLD_MIN_Y_MM; s.vy_mm_s = 0; }
        if (s.y_mm > WORLD_MAX_Y_MM) { s.y_mm = WORLD_MAX_Y_MM; s.vy_mm_s = 0; }
        if (s.z_mm < WORLD_MIN_Z_MM) { s.z_mm = WORLD_MIN_Z_MM; s.vz_mm_s = 0; }
        if (s.z_mm > WORLD_MAX_Z_MM) { s.z_mm = WORLD_MAX_Z_MM; s.vz_mm_s = 0; }

        // Publish state to other subsystems
        xQueueOverwrite(flocking_q,  &s);
        xQueueOverwrite(radio_q,     &s);
        xQueueOverwrite(telemetry_q, &s);
        // physics.c, inside while(true) after xQueueOverwrite(...)
    }
}

void init_physics(void)
{
    xTaskCreate(physics_task,
                PHYSICS_TASK_NAME,
                PHYSICS_MEM,
                NULL,
                PHYSICS_PRIORITY,
                NULL);
}
