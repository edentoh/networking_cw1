// main/security.c
#include "tasks.h"
#include "config.h"

#include "esp_err.h"
#include "mbedtls/cmac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <math.h>   // For sqrt
#include <stdlib.h> // For abs

typedef struct {
    uint8_t    node_id[6];
    bool       in_use;
    
    // Rate Limiting
    TickType_t last_rx_tick;

    // Physics & Replay State
    uint32_t   last_ts_s;
    uint16_t   last_ts_ms;
    uint16_t   last_seq;
    
    int32_t    last_x_mm;
    int32_t    last_y_mm;
    int32_t    last_z_mm;
} SecurityEntry;

static SecurityEntry SECURITY_TABLE[MAX_TRACKED_NODES];

// 16-byte pre-shared key
static const uint8_t s_aes_key[16] = {
    0x2B, 0x7E, 0x15, 0x16,
    0x22, 0xA0, 0xD2, 0xA6,
    0xAC, 0xF7, 0x19, 0x88,
    0x09, 0xCF, 0x4F, 0x3C
};

// -----------------------------------------------------------------------------
// HELPER: Get total milliseconds from seconds + ms parts
// -----------------------------------------------------------------------------
static uint64_t to_ms(uint32_t s, uint16_t ms) {
    return ((uint64_t)s * 1000ULL) + ms;
}

// -----------------------------------------------------------------------------
// CORE VALIDATION FUNCTION
// Returns TRUE if packet is valid, FALSE if attack detected.
// -----------------------------------------------------------------------------
bool security_validate_packet(const NeighbourState *n)
{
    // -------------------------------------------------------------------------
    // 1. PROTOCOL CHECK
    // -------------------------------------------------------------------------
    if (n->version != VERSION) {
        fast_log("SEC (W): Invalid version %u (Expected %u)", n->version, VERSION);
        return false;
    }

    // -------------------------------------------------------------------------
    // 2. TABLE LOOKUP & STATEFUL CHECKS
    // -------------------------------------------------------------------------
    TickType_t now_tick = xTaskGetTickCount();
    SecurityEntry *entry = NULL;
    int first_empty = -1;

    // Find entry
    for (int i = 0; i < MAX_TRACKED_NODES; ++i) {
        if (SECURITY_TABLE[i].in_use) {
            if (memcmp(SECURITY_TABLE[i].node_id, n->node_id, 6) == 0) {
                entry = &SECURITY_TABLE[i];
                break;
            }
        } else {
            if (first_empty < 0) first_empty = i;
        }
    }

    // If new node
    if (!entry) {
        if (first_empty < 0) {
            fast_log("SEC (E): Table full, dropping %s", format_mac(n->node_id));
            return false;
        }
        entry = &SECURITY_TABLE[first_empty];
        
        // Init entry
        entry->in_use = true;
        memcpy(entry->node_id, n->node_id, 6);
        entry->last_rx_tick = now_tick;
        entry->last_seq     = n->seq_number;
        entry->last_ts_s    = n->ts_s;
        entry->last_ts_ms   = n->ts_ms;
        entry->last_x_mm    = n->x_mm;
        entry->last_y_mm    = n->y_mm;
        entry->last_z_mm    = n->z_mm;

        return true; // First packet is trusted (baseline)
    }

    // -------------------------------------------------------------------------
    // 3. RATE LIMITING (DDoS)
    // -------------------------------------------------------------------------
    TickType_t tick_diff = now_tick - entry->last_rx_tick;
    if (tick_diff < pdMS_TO_TICKS(DDOS_RATE_LIMIT_MS)) {
        // fast_log("SEC (W): Rate limit exceeded for %s", format_mac(n->node_id));
        return false;
    }

    // -------------------------------------------------------------------------
    // 4. SEQUENCE / REPLAY CHECK
    // -------------------------------------------------------------------------
    // We allow wrap-around logic or strict > check. Simple > is safest for lab.
    // Also check timestamps to ensure time moves forward.
    
    // Note: If sender rebooted, seq resets. This logic drops packets until 
    // seq catches up or we timeout the entry. Simple fix: If seq drops drastically
    // but time is valid, maybe reset? For now, strict check:
    if (n->seq_number <= entry->last_seq && 
       (entry->last_seq - n->seq_number) < 1000) { // Not a wrap-around
        fast_log("SEC (W): Replay/Old Seq %u <= %u from %s", 
                 n->seq_number, entry->last_seq, format_mac(n->node_id));
        return false;
    }

    uint64_t time_old = to_ms(entry->last_ts_s, entry->last_ts_ms);
    uint64_t time_new = to_ms(n->ts_s, n->ts_ms);

    if (time_new <= time_old) {
        fast_log("SEC (W): Replay/Old Time from %s", format_mac(n->node_id));
        return false;
    }

    // -------------------------------------------------------------------------
    // 5. PHYSICS CHECK (Prevent "Random Pos" / Teleportation)
    // -------------------------------------------------------------------------
    
    // Calculate distance moved (mm)
    double dx = (double)n->x_mm - entry->last_x_mm;
    double dy = (double)n->y_mm - entry->last_y_mm;
    double dz = (double)n->z_mm - entry->last_z_mm;
    double dist_mm = sqrt(dx*dx + dy*dy + dz*dz);

    // Calculate time elapsed (seconds)
    double dt_sec = (double)(time_new - time_old) / 1000.0;

    // Calculate implied velocity (mm/s)
    double velocity = 0.0;
    if (dt_sec > 0.001) {
        velocity = dist_mm / dt_sec;
    } else {
        // Zero time elapsed?
        if (dist_mm > PHYSICS_JUMP_TOLERANCE_MM) {
             fast_log("SEC (W): Teleport (Instant Jump %.0fmm) %s", 
                      dist_mm, format_mac(n->node_id));
             return false;
        }
    }

    double max_speed = MAX_SPEED_MM_S * PHYSICS_SPEED_FACTOR;
    
    // Check limit
    if (velocity > max_speed) {
        fast_log("SEC (W): Physics Violation! Speed %.0f > %.0f mm/s by %s", 
                 velocity, max_speed, format_mac(n->node_id));
        return false;
    }

    // -------------------------------------------------------------------------
    // UPDATE STATE
    // -------------------------------------------------------------------------
    entry->last_rx_tick = now_tick;
    entry->last_seq     = n->seq_number;
    entry->last_ts_s    = n->ts_s;
    entry->last_ts_ms   = n->ts_ms;
    entry->last_x_mm    = n->x_mm;
    entry->last_y_mm    = n->y_mm;
    entry->last_z_mm    = n->z_mm;

    return true;
}

// -----------------------------------------------------------------------------
// AES-CMAC Crypto
// -----------------------------------------------------------------------------

static void compute_mac(const uint8_t *buf, size_t len, uint8_t out[4])
{
    uint8_t full_mac[16];

    const mbedtls_cipher_info_t *info =
        mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);

    int ret = mbedtls_cipher_cmac(info,
                                  s_aes_key, 128,
                                  buf, len,
                                  full_mac);
    if (ret != 0) {
        fast_log("SECURITY (E): CMAC failed (%d)", ret);
        memset(out, 0, 4);
        return;
    }

    memcpy(out, full_mac + 12, 4);
}

void sign_packet(NeighbourState *state)
{
    uint8_t mac[4];
    compute_mac((const uint8_t*)state,
                offsetof(NeighbourState, mac_tag),
                mac);
    memcpy(state->mac_tag, mac, 4);
}

bool verify_packet(NeighbourState *state)
{
    uint8_t expected[4];
    compute_mac((const uint8_t*)state,
                offsetof(NeighbourState, mac_tag),
                expected);

    uint8_t diff = 0;
    for (int i = 0; i < 4; ++i) {
        diff |= (uint8_t)(expected[i] ^ state->mac_tag[i]);
    }
    return diff == 0;
}