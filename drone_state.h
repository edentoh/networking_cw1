// main/drone_state.h
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Continuous state of our own drone (simulator)
typedef struct {
    double x_mm;
    double y_mm;
    double z_mm;

    double vx_mm_s;
    double vy_mm_s;
    double vz_mm_s;

    double yaw_cd;          // centidegrees
    double yaw_rate_cd_s;   // centidegrees per second
} DroneState;

// Command from flocking → physics
typedef struct {
    double target_vx_mm_s;
    double target_vy_mm_s;
    double target_vz_mm_s;
    double target_yaw_rate_cd_s;
} ControlInput;

// Packed state sent over radio / MQTT
typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  team_id;
    uint8_t  node_id[6];

    uint16_t seq_number;

    uint32_t ts_s;
    uint16_t ts_ms;

    uint32_t x_mm;
    uint32_t y_mm;
    uint32_t z_mm;

    int32_t  vx_mm_s;
    int32_t  vy_mm_s;
    int32_t  vz_mm_s;

    uint16_t yaw_cd;

    uint8_t  mac_tag[4];
} NeighbourState;

// Convert simulator state → on-wire packet (MAC is added later)
NeighbourState DroneState_to_NeighbourState(DroneState *own_state,
                                            uint16_t seq_number);

// Debug helper (optional)
void print_neighbour_state(const NeighbourState *state);
// main/drone_state.h

void log_drone_state(const char *tag, const DroneState *s);
void log_neighbour_state(const char *tag, const NeighbourState *n);
const char *format_mac(const uint8_t mac[6]);  // short helper; see below
void log_radio_packet(const char *direction, const NeighbourState *n);

#ifdef __cplusplus
}
#endif
