// main/flocking.c
#include "tasks.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

typedef struct {
    bool          is_valid;
    uint32_t      last_updated_s;
    NeighbourState neighbour_state;
} NeighbourEntry;

static NeighbourEntry NEIGHBOUR_TABLE[MAX_NEIGHBOURS];

static void prune_stale_neighbours(void)
{
    uint32_t now_s; uint16_t now_ms;
    get_current_unix_time(&now_s, &now_ms);

    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (!NEIGHBOUR_TABLE[i].is_valid)
            continue;

        uint32_t age = now_s - NEIGHBOUR_TABLE[i].last_updated_s;
        if (age > NEIGHBOUR_STALE_TIMEOUT_S) {
            fast_log("FLOCKING (I): neighbour %s stale (age=%us) -> removed",
                     format_mac(NEIGHBOUR_TABLE[i].neighbour_state.node_id),
                     (unsigned)age);
            NEIGHBOUR_TABLE[i].is_valid = false;
        }
    }
}

static void update_neighbour_table(const NeighbourState *n)
{
    int first_empty = -1;
    uint32_t now_s; uint16_t now_ms;
    get_current_unix_time(&now_s, &now_ms);
    // Don't treat ourselves as a neighbour
    if (memcmp(n->node_id, get_mac_address(), 6) == 0) {
        return;
    }

    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (NEIGHBOUR_TABLE[i].is_valid &&
            memcmp(NEIGHBOUR_TABLE[i].neighbour_state.node_id,
                   n->node_id, sizeof(n->node_id)) == 0) {

            // Simple seq check: ignore old packets
            if (n->seq_number > NEIGHBOUR_TABLE[i].neighbour_state.seq_number) {
                NEIGHBOUR_TABLE[i].neighbour_state = *n;
                NEIGHBOUR_TABLE[i].last_updated_s  = now_s;
            }
            return;
        }
        if (!NEIGHBOUR_TABLE[i].is_valid && first_empty < 0)
            first_empty = i;
    }

    int idx = (first_empty >= 0) ? first_empty : 0; // simple eviction
    NEIGHBOUR_TABLE[idx].is_valid = true;
    NEIGHBOUR_TABLE[idx].last_updated_s = now_s;
    NEIGHBOUR_TABLE[idx].neighbour_state = *n;
}

static ControlInput compute_control(const DroneState *self)
{
    ControlInput u = {
        .target_vx_mm_s = self->vx_mm_s,
        .target_vy_mm_s = self->vy_mm_s,
        .target_vz_mm_s = self->vz_mm_s,
        .target_yaw_rate_cd_s = 0.0
    };

    double sep_x = 0, sep_y = 0, sep_z = 0;
    double ali_vx = 0, ali_vy = 0, ali_vz = 0;
    double coh_x = 0, coh_y = 0, coh_z = 0;

    int count = 0;

    for (int i = 0; i < MAX_NEIGHBOURS; ++i) {
        if (!NEIGHBOUR_TABLE[i].is_valid) continue;
        const NeighbourState *n = &NEIGHBOUR_TABLE[i].neighbour_state;

        double dx = (double)n->x_mm - self->x_mm;
        double dy = (double)n->y_mm - self->y_mm;
        double dz = (double)n->z_mm - self->z_mm;

        double dist2 = dx*dx + dy*dy + dz*dz;
        if (dist2 > FLOCKING_NEIGHBOUR_RADIUS_MM * FLOCKING_NEIGHBOUR_RADIUS_MM)
            continue;

        ++count;
        double dist = sqrt(dist2) + 1e-6;

        // separation
        sep_x -= dx / dist;
        sep_y -= dy / dist;
        sep_z -= dz / dist;

        // alignment
        ali_vx += n->vx_mm_s;
        ali_vy += n->vy_mm_s;
        ali_vz += n->vz_mm_s;

        // cohesion (positions)
        coh_x += n->x_mm;
        coh_y += n->y_mm;
        coh_z += n->z_mm;
    }

    if (count > 0) {
        sep_x /= count; sep_y /= count; sep_z /= count;
        ali_vx = ali_vx / count - self->vx_mm_s;
        ali_vy = ali_vy / count - self->vy_mm_s;
        ali_vz = ali_vz / count - self->vz_mm_s;
        coh_x  = (coh_x / count) - self->x_mm;
        coh_y  = (coh_y / count) - self->y_mm;
        coh_z  = (coh_z / count) - self->z_mm;

        u.target_vx_mm_s += FLOCKING_SEPARATION_GAIN * sep_x
                          + FLOCKING_ALIGNMENT_GAIN  * ali_vx
                          + FLOCKING_COHESION_GAIN   * coh_x;
        u.target_vy_mm_s += FLOCKING_SEPARATION_GAIN * sep_y
                          + FLOCKING_ALIGNMENT_GAIN  * ali_vy
                          + FLOCKING_COHESION_GAIN   * coh_y;
        u.target_vz_mm_s += FLOCKING_SEPARATION_GAIN * sep_z
                          + FLOCKING_ALIGNMENT_GAIN  * ali_vz
                          + FLOCKING_COHESION_GAIN   * coh_z;
    }

    // -------------------------------------------------------------------------
    // Speed Limiting
    // -------------------------------------------------------------------------
    double v2 = u.target_vx_mm_s*u.target_vx_mm_s +
                u.target_vy_mm_s*u.target_vy_mm_s +
                u.target_vz_mm_s*u.target_vz_mm_s;
    double vmax2 = MAX_SPEED_MM_S * (double)MAX_SPEED_MM_S;
    
    if (v2 > vmax2) {
        double scale = MAX_SPEED_MM_S / sqrt(v2);
        u.target_vx_mm_s *= scale;
        u.target_vy_mm_s *= scale;
        u.target_vz_mm_s *= scale;
    }

    // -------------------------------------------------------------------------
    // NEW: Compute Yaw (Face Velocity Strategy)
    // -------------------------------------------------------------------------
    // Only update yaw if we are moving significantly (> 50 mm/s)
    // otherwise atan2 becomes unstable and the drone jitters.
    double speed_sq = u.target_vx_mm_s*u.target_vx_mm_s + 
                      u.target_vy_mm_s*u.target_vy_mm_s;

    if (speed_sq > (50.0 * 50.0)) {
        // 1. Calculate the desired heading based on velocity vector
        // atan2 returns radians between -pi and pi
        double target_heading_rad = atan2(u.target_vy_mm_s, u.target_vx_mm_s);
        double target_heading_deg = target_heading_rad * (180.0 / M_PI);

        // 2. Get current heading in degrees
        double current_heading_deg = (double)self->yaw_cd / 100.0;

        // 3. Calculate error (shortest path)
        double error_deg = target_heading_deg - current_heading_deg;
        
        // Normalize to [-180, 180]
        while (error_deg > 180.0)  error_deg -= 360.0;
        while (error_deg < -180.0) error_deg += 360.0;

        // 4. Proportional Controller (Gain = 2.0 works well for responsiveness)
        // Output is in centi-degrees/sec
        double kP_yaw = 2.0; 
        u.target_yaw_rate_cd_s = (int32_t)(error_deg * kP_yaw * 100.0);
        
        // Clamp yaw rate (e.g., max 90 deg/s = 9000 cd/s)
        if (u.target_yaw_rate_cd_s > 9000) u.target_yaw_rate_cd_s = 9000;
        if (u.target_yaw_rate_cd_s < -9000) u.target_yaw_rate_cd_s = -9000;

    } else {
        // If hovering/moving slowly, hold current heading (rate = 0)
        u.target_yaw_rate_cd_s = 0;
    }

    return u;
}

static void flocking_task(void *arg)
{
    (void)arg;

    QueueHandle_t neigh_q  = get_neighbour_update_queue();
    QueueHandle_t state_q  = get_flocking_state_queue();
    QueueHandle_t control_q = get_control_input_queue();

    TickType_t period = pdMS_TO_TICKS(FLOCKING_PERIOD_MS);
    TickType_t next   = xTaskGetTickCount();

    DroneState self = {0};

    while (true) {
        vTaskDelayUntil(&next, period);

        prune_stale_neighbours();

        // ingest neighbour updates (drain quickly)
        NeighbourState n;
        while (xQueueReceive(neigh_q, &n, 0) == pdTRUE) {
            update_neighbour_table(&n);
            log_neighbour_state("FLOCKING NEIGH UPDATE", &n);
        }


        // latest own state
        if (xQueueReceive(state_q, &self, 0) == pdTRUE) {
            ControlInput u = compute_control(&self);
            xQueueOverwrite(control_q, &u);

            static int tick = 0;
            tick++;
            if (tick % 10 == 0) {  // every ~1 s at 10 Hz
                log_drone_state("FLOCKING OWN", &self);
                fast_log("FLOCKING CMD: vx=%.1f vy=%.1f vz=%.1f yaw_rate=%.1f",
                        u.target_vx_mm_s, u.target_vy_mm_s,
                        u.target_vz_mm_s, u.target_yaw_rate_cd_s);
            }
        }
    }
}

void init_flocking(void)
{
    memset(NEIGHBOUR_TABLE, 0, sizeof(NEIGHBOUR_TABLE));

    xTaskCreate(flocking_task,
                FLOCKING_TASK_NAME,
                FLOCKING_MEM,
                NULL,
                FLOCKING_PRIORITY,
                NULL);
}
