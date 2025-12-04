// main/attacker.c
#include "tasks.h"
#include "config.h"
#include "drone_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// -----------------------------------------------------------------------------
// Helper: Generate a base packet
// -----------------------------------------------------------------------------
static NeighbourState create_base_packet(void)
{
    NeighbourState pkt;
    memset(&pkt, 0, sizeof(pkt));

    // Standard valid headers
    pkt.version = VERSION;
    pkt.team_id = TEAM_ID;
    
    // Use our own MAC by default
    memcpy(pkt.node_id, get_mac_address(), 6);

    // Current valid time
    get_current_unix_time(&pkt.ts_s, &pkt.ts_ms);

    // Dummy physics data
    pkt.x_mm = 50000; pkt.y_mm = 50000; pkt.z_mm = 1000;
    pkt.vx_mm_s = 100;

    return pkt;
}

// -----------------------------------------------------------------------------
// Attack 1: FLOODING (DDoS)
// Sends packets at 50Hz (20ms interval) with VALID signature and fixed state.
// This forces the receiver to verify the crypto, then reject based on Rate Limit.
// -----------------------------------------------------------------------------
static void run_flood_attack(QueueHandle_t attack_q)
{
    fast_log("ATTACK (I): Starting FLOOD (DDoS) attack (Signed)...");
    
    NeighbourState pkt = create_base_packet();
    
    // Send 1000 packets rapidly
    for (int i = 0; i < 1000; ++i) {
        pkt.seq_number++;
        get_current_unix_time(&pkt.ts_s, &pkt.ts_ms);
        
        // --- RESTORED: VALID SIGNATURE ---
        // We sign the packet correctly. This passes the Crypto check on the receiver.
        // The receiver must then use the Rate Limiter (Security Table) to drop it.
        sign_packet(&pkt);

        // --- RESTORED: FIXED POS/VEL ---
        // We do NOT randomize positions. We keep the static values from create_base_packet.
        // (x=50000, y=50000, z=1000)

        // Push to radio
        xQueueSend(attack_q, &pkt, 0);

        // Wait only 20ms (50Hz) -> Should trigger Rate Limiter on Receiver (limit is ~200ms)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    
    fast_log("ATTACK (I): FLOOD attack finished.");
}

// -----------------------------------------------------------------------------
// Attack 2: REPLAY (Old Timestamp)
// Sends a packet with a valid signature but a timestamp from 1 hour ago.
// -----------------------------------------------------------------------------
static void run_replay_attack(QueueHandle_t attack_q)
{
    fast_log("ATTACK (I): Starting REPLAY attack...");

    NeighbourState pkt = create_base_packet();
    pkt.seq_number = 9999;
    
    // Set time to 1 hour ago (3600 seconds)
    get_current_unix_time(&pkt.ts_s, &pkt.ts_ms);
    pkt.ts_s -= 10; 

    // Sign it (signature is valid for this data!)
    sign_packet(&pkt);

    // Send a few copies
    for (int i = 0; i < 5; ++i) {
        xQueueSend(attack_q, &pkt, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    fast_log("ATTACK (I): REPLAY attack finished.");
}

// -----------------------------------------------------------------------------
// Attack 3: SPOOFING (Bad Signature / Fake MAC)
// Sends a packet that looks valid but has a corrupted signature or unknown MAC.
// -----------------------------------------------------------------------------
static void run_spoof_attack(QueueHandle_t attack_q)
{
    fast_log("ATTACK (I): Starting SPOOF attack (Bad Signature)...");

    NeighbourState pkt = create_base_packet();
    
    // Use a fake MAC address (0xDEADBEEF...)
    uint8_t fake_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    memcpy(pkt.node_id, fake_mac, 6);

    sign_packet(&pkt);

    // CORRUPT THE SIGNATURE manually after signing
    pkt.mac_tag[0] ^= 0xFF; 
    pkt.mac_tag[3] ^= 0xFF;

    // Send
    for (int i = 0; i < 5; ++i) {
        xQueueSend(attack_q, &pkt, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    fast_log("ATTACK (I): SPOOF attack finished.");
}

// -----------------------------------------------------------------------------
// Main Attacker Task
// -----------------------------------------------------------------------------
static void attacker_task(void *arg)
{
    (void)arg;
    QueueHandle_t attack_q = get_radio_state_queue(); // We need a NEW queue really, see Step 2

    // Wait for system to stabilize
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true) {
        // 1. Flood Attack
        run_flood_attack(get_attack_queue()); 
        vTaskDelay(pdMS_TO_TICKS(10000)); // Rest

        // 2. Replay Attack
        run_replay_attack(get_attack_queue());
        vTaskDelay(pdMS_TO_TICKS(5000));

        // 3. Spoof Attack
        run_spoof_attack(get_attack_queue());
        vTaskDelay(pdMS_TO_TICKS(10000)); // Long rest before repeating
    }
}

void init_attacker(void)
{
    xTaskCreate(attacker_task,
                ATTACK_TASK_NAME,
                ATTACK_MEM,
                NULL,
                ATTACK_PRIORITY,
                NULL);
}