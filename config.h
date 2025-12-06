// main/config.h
#pragma once

// =============================================================================
//  1. WIFI & NETWORK CONFIGURATION
// =============================================================================

// 0 - Home WiFi, 1 - Eduroam
#define USE_EDUROAM 1

#if USE_EDUROAM
    #define WIFI_SSID        "eduroam"
    #define EDUROAM_IDENTITY ""
    #define EDUROAM_USERNAME ""
    #define EDUROAM_PASSWORD ""
#else
    #define WIFI_SSID        ""
    #define WIFI_PASSWORD    ""
#endif

#define WIFI_CONNECT_TIMEOUT_MS 30000

// =============================================================================
//  2. MQTT BROKER CONFIGURATION
// =============================================================================

#define BROKER_URI              "mqtt://broker.hivemq.com:1883"
#define MQTT_TOPIC              "flocksim"

// Aliases for compatibility with comms_mqtt.c
#define MQTT_BROKER_URI         BROKER_URI

// =============================================================================
//  3. SIMULATION & WORLD BOUNDS
// =============================================================================

#define VERSION                 1
#define TEAM_ID                 1
#define MAX_JSON_STRING_LENGTH  1024

// World Bounds (mm)
#define WORLD_MIN_X_MM          0.0
#define WORLD_MAX_X_MM          100000.0
#define WORLD_MIN_Y_MM          0.0
#define WORLD_MAX_Y_MM          100000.0
#define WORLD_MIN_Z_MM          0.0
#define WORLD_MAX_Z_MM          100000.0

// =============================================================================
//  4. FLOCKING PHYSICS & BEHAVIOUR
// =============================================================================

#define MAX_NEIGHBOURS                  50
#define NEIGHBOUR_TIMEOUT_MS            30000
#define NEIGHBOUR_STALE_TIMEOUT_S       (NEIGHBOUR_TIMEOUT_MS / 1000)

// Physics Limits
#define MAX_SPEED_MM_S                  800.0
#define SEPARATION_RADIUS_MM            5000.0
#define FLOCKING_NEIGHBOUR_RADIUS_MM    141000.0

// Flocking Gains (Tunable)
#define FLOCKING_ALIGNMENT_GAIN         0.1
#define FLOCKING_COHESION_GAIN          0.08
#define FLOCKING_SEPARATION_GAIN        8.0

// =============================================================================
//  5. LOGGING CONFIGURATION
// =============================================================================

#define LOGGING_ENABLED                 1
#define MAX_LOG_MSG_LEN                 120
#define LOG_MESSAGE_QUEUE_LENGTH        32

// =============================================================================
//  6. TASK CONFIGURATION (Priorities, Stacks, Timing)
// =============================================================================

// --- Logger Task ---
#define LOGGER_TASK_NAME          "log"
#define LOGGER_MEM                2048
#define LOGGER_PRIORITY           1
#define LOGGER_TASK_PRIORITY      1
#define LOGGER_FREQ_HZ            5
#define LOGGER_PERIOD_MS          (1000 / LOGGER_FREQ_HZ)

// --- Physics Task (50Hz) ---
#define PHYSICS_TASK_NAME         "physics"
#define PHYSICS_MEM               3072
#define PHYSICS_PRIORITY          7
#define PHYSICS_FREQ_HZ           50
#define PHYSICS_PERIOD_MS         (1000 / PHYSICS_FREQ_HZ)

// --- Flocking Task (10Hz) ---
#define FLOCKING_TASK_NAME        "flocking"
#define FLOCKING_MEM              4096
#define FLOCKING_PRIORITY         6
#define FLOCKING_FREQ_HZ          10
#define FLOCKING_PERIOD_MS        (1000 / FLOCKING_FREQ_HZ)

// --- Radio Task (LoRa) ---
// Using "Combined" task style (RX/TX in one loop)
#define RADIO_COMBINED_TASK_NAME  "radio_rx_tx"
#define RADIO_COMBINED_MEM        8192
#define RADIO_COMBINED_PRIORITY   5

// Requirement: 2-5Hz. We set to 2Hz.
#define RADIO_TX_FREQ_HZ          0.2
#define RADIO_TX_PERIOD_MS        (1000 / RADIO_TX_FREQ_HZ)

// --- MQTT Telemetry Task ---
#define MQTT_TELEMETRY_TASK_NAME  "mqtt"
#define MQTT_TELEMETRY_MEM        4096
#define MQTT_TELEMETRY_PRIORITY   3

// Requirement: 2Hz.
#define TELEMETRY_FREQ_HZ         5
#define TELEMETRY_PERIOD_MS       (1000 / TELEMETRY_FREQ_HZ)

// Alias for comms_mqtt.c
#define MQTT_TELEMETRY_PERIOD_MS  TELEMETRY_PERIOD_MS

// =============================================================================
//  7. SECURITY CONFIGURATION
// =============================================================================
#define DDOS_RATE_LIMIT_MS   1000
#define MAX_TRACKED_NODES    MAX_NEIGHBOURS

// Physics tolerance: How much faster than MAX_SPEED can a node seemingly move 
// before we call it fake? (Factors: latency, packet loss, small jumps)
#define PHYSICS_SPEED_FACTOR 3.0 
#define PHYSICS_JUMP_TOLERANCE_MM 500 // Allow 0.5m jitter even at 0 time diff

// =============================================================================
//  8. ADVERSARIAL / ATTACK CONFIGURATION
// =============================================================================

// Set to 1 to enable Attack Mode (Flood/Replay/Spoof)
// Set to 0 to run as a normal compliant drone
#define ENABLE_ATTACK_TASK      1
#define ATTACK_TASK_NAME       "attacker"
#define ATTACK_MEM             4096
#define ATTACK_PRIORITY        4  // Lower than Radio/Physics to not starve them

// =============================================================================
// MONITORING CONFIGURATION
// =============================================================================
#define MONITOR_REPORT_PERIOD_MS 10000  // Print report every 10 seconds
#define EST_CURRENT_BASE_MA      100   // ESP32 + WiFi (Active)
#define EST_CURRENT_LORA_RX_MA   12    // SX1276 RX

#define EST_CURRENT_LORA_TX_MA   45    // SX1276 TX (14dBm)
