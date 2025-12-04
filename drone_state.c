// main/drone_state.c
#include "drone_state.h"
#include "config.h"
#include "tasks.h"   // for get_mac_address + time + logging

#include <string.h>

NeighbourState DroneState_to_NeighbourState(DroneState* own_state,
                                            uint16_t seq_number)
{
    NeighbourState out = {0};

    // Timestamp (NTP-synced time)
    uint32_t ts_s = 0;
    uint16_t ts_ms = 0;
    get_current_unix_time(&ts_s, &ts_ms);
    out.ts_s  = ts_s;
    out.ts_ms = ts_ms;

    out.version   = VERSION;
    out.team_id   = TEAM_ID;
    memcpy(out.node_id, get_mac_address(), sizeof(out.node_id));
    out.seq_number = seq_number;

    out.x_mm    = (uint32_t)own_state->x_mm;
    out.y_mm    = (uint32_t)own_state->y_mm;
    out.z_mm    = (uint32_t)own_state->z_mm;

    out.vx_mm_s = (int32_t)own_state->vx_mm_s;
    out.vy_mm_s = (int32_t)own_state->vy_mm_s;
    out.vz_mm_s = (int32_t)own_state->vz_mm_s;

    out.yaw_cd  = (uint16_t)own_state->yaw_cd;


    // mac_tag will be filled by sign_packet()

    return out;
}

static char mac_buf[18];

const char *format_mac(const uint8_t mac[6])
{
    snprintf(mac_buf, sizeof(mac_buf),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return mac_buf;
}

void log_radio_packet(const char *direction, const NeighbourState *n)
{
    // direction: "TX" or "RX"
    fast_log("RADIO %s | node=%s seq=%u "
             "pos=(%u,%u,%u) vel=(%ld,%ld,%ld) yaw=%u",
             direction,
             format_mac(n->node_id),
             (unsigned)n->seq_number,
             (unsigned)n->x_mm,
             (unsigned)n->y_mm,
             (unsigned)n->z_mm,
             (long)n->vx_mm_s,
             (long)n->vy_mm_s,
             (long)n->vz_mm_s,
             (unsigned)n->yaw_cd);
}

void print_neighbour_state(const NeighbourState* s)
{
    fast_log("NeighbourState:");
    fast_log("  node_id=%02X%02X%02X%02X%02X%02X seq=%u",
             s->node_id[0], s->node_id[1], s->node_id[2],
             s->node_id[3], s->node_id[4], s->node_id[5],
             (unsigned)s->seq_number);
    fast_log("  pos=(%ld,%ld,%ld) vel=(%ld,%ld,%ld) yaw=%ld",
             (long)s->x_mm, (long)s->y_mm, (long)s->z_mm,
             (long)s->vx_mm_s, (long)s->vy_mm_s, (long)s->vz_mm_s,
             (long)s->yaw_cd);
}

// main/drone_state.c

void log_drone_state(const char *tag, const DroneState *s)
{
    fast_log("%s: pos=(%.1f, %.1f, %.1f) vel=(%.1f, %.1f, %.1f) yaw=%.1f cd",
             tag,
             s->x_mm, s->y_mm, s->z_mm,
             s->vx_mm_s, s->vy_mm_s, s->vz_mm_s,
             s->yaw_cd);
}

void log_neighbour_state(const char *tag, const NeighbourState *n)
{
    fast_log(
        "%s: node=%02X%02X%02X%02X%02X%02X seq=%u "
        "pos=(%u,%u,%u) vel=(%ld,%ld,%ld) yaw=%u ts=%lu.%03u",
        tag,
        n->node_id[0], n->node_id[1], n->node_id[2],
        n->node_id[3], n->node_id[4], n->node_id[5],
        (unsigned)n->seq_number,
        (unsigned)n->x_mm, (unsigned)n->y_mm, (unsigned)n->z_mm,
        (long)n->vx_mm_s, (long)n->vy_mm_s, (long)n->vz_mm_s,
        (unsigned)n->yaw_cd,
        (unsigned long)n->ts_s, (unsigned)n->ts_ms);
}

