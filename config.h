// This file contains various task configuration values

// ------------------------------------------------------------ WiFi
// 0 - Home WiFi
// 1 - Eduroam
#define USE_EDUROAM 1

#if USE_EDUROAM
    #define WIFI_SSID        "eduroam"
    #define EDUROAM_IDENTITY "zcabbzt@ucl.ac.uk"
    #define EDUROAM_USERNAME "zcabbzt@ucl.ac.uk"
    #define EDUROAM_PASSWORD "EdenT@h313032bz."
#else
    #define WIFI_SSID     "wifi"
    #define WIFI_PASSWORD "123456789"
#endif

#define WIFI_CONNECT_TIMEOUT_MS 30000


// ------------------------------------------------------------ MQTT broker config
// Main names:
#define BROKER_URI "mqtt://broker.hivemq.com:1883"
// #define BROKER_URI "mqtt://engf0001.cs.ucl.ac.uk:1883"
#define MQTT_TOPIC  "flocksim"

// Aliases used by comms_mqtt.c (simplified code)
#define MQTT_BROKER_URI       BROKER_URI
#define MQTT_TELEMETRY_PERIOD_MS TELEMETRY_PERIOD_MS


// ------------------------------------------------------------ Logging
#define MAX_LOG_MSG_LEN            100
// Maximum number of logs in queue
#define LOG_MESSAGE_QUEUE_LENGTH   32

// 0 - disabled
// 1 - enabled
#define LOGGING_ENABLED 1

#define ENABLE_PHYSICS_STATS  1
#define ENABLE_RADIO_STATS    1
#define ENABLE_FLOCKING_STATS 1
#define ENABLE_MQTT_STATS     1


// ------------------------------------------------------------ Task config
//                                                              Logger task
#define LOGGER_TASK_NAME  "log"
#define LOGGER_MEM        2048
#define LOGGER_PRIORITY   1
#define LOGGER_FREQ_HZ    5
#define LOGGER_PERIOD_MS  (1.0 / (LOGGER_FREQ_HZ) * 1000.0)
#define LOGGER_TASK_PRIORITY 1


//                                                              Physics task
#define PHYSICS_TASK_NAME "physics"
#define PHYSICS_MEM       3072
#define PHYSICS_PRIORITY  7
#define PHYSICS_FREQ_HZ   50
#define PHYSICS_PERIOD_MS (1.0 / (PHYSICS_FREQ_HZ) * 1000.0)

// How many samples to collect before logging stats
#define PHYSICS_STATS_REPORT_INTERVAL 1000


//                                                              Flocking controller task
#define FLOCKING_TASK_NAME "flocking"
#define FLOCKING_MEM       4096
#define FLOCKING_PRIORITY  6
#define FLOCKING_FREQ_HZ   10
#define FLOCKING_PERIOD_MS (1.0 / (FLOCKING_FREQ_HZ) * 1000.0)

// Fraction of period up to which neighbour updates are processed
#define UPDATE_NEIGHBOURS_BUDGET_FRAC 0.33

// How many samples to collect before logging stats
#define FLOCKING_STATS_REPORT_INTERVAL 200

#define MAX_NEIGHBOURS      50
#define NEIGHBOUR_TIMEOUT_MS 20000   // 20 seconds

// Flocking behaviour config (original names)
#define ALIGNMENT_WEIGHT     0.08
#define COHESION_WEIGHT      0.1
#define FLOCKING_RADIUS_MM   141000.0
#define MAX_SPEED_MM_S       500.0
#define SEPARATION_RADIUS_MM 5000.0
#define SEPARATION_WEIGHT    0.04

// Aliases used by simplified flocking.c
#define FLOCKING_ALIGNMENT_GAIN     ALIGNMENT_WEIGHT
#define FLOCKING_COHESION_GAIN      COHESION_WEIGHT
#define FLOCKING_SEPARATION_GAIN    SEPARATION_WEIGHT
#define FLOCKING_NEIGHBOUR_RADIUS_MM FLOCKING_RADIUS_MM
// Flocking code expects timeout in seconds
#define NEIGHBOUR_STALE_TIMEOUT_S   (NEIGHBOUR_TIMEOUT_MS / 1000)


// World bounds
#define WORLD_MIN_X_MM 0.0
#define WORLD_MAX_X_MM 100000.0
#define WORLD_MIN_Y_MM 0.0
#define WORLD_MAX_Y_MM 100000.0
#define WORLD_MIN_Z_MM 0.0
#define WORLD_MAX_Z_MM 100000.0


//                                                              Radio task
// 0 - single task
// 1 - dual task
#define RADIO_TASK_TYPE   1

#define RADIO_TX_FREQ_HZ  (1.0 / 6.0)
#define RADIO_RX_FREQ_HZ  5  // Only effective if dual task mode is used

#define RADIO_TX_PERIOD_MS (1.0 / (RADIO_TX_FREQ_HZ) * 1000.0)
#define RADIO_RX_PERIOD_MS (1.0 / (RADIO_RX_FREQ_HZ) * 1000.0)

// Fraction of period up to which packets are received
#define RADIO_RX_BUDGET_FRAC 0.5

// How many samples to collect before logging stats
#define RADIO_TX_STATS_REPORT_INTERVAL 3
#define RADIO_RX_STATS_REPORT_INTERVAL 100

#define RADIO_COMBINED_TASK_NAME  "radio_rx_tx"
#define RADIO_COMBINED_MEM        8192
#define RADIO_COMBINED_PRIORITY   5

#define RADIO_RX_TASK_NAME        "radio_rx"
#define RADIO_RX_MEM              4096
#define RADIO_RX_PRIORITY         4

#define RADIO_TX_TASK_NAME        "radio_tx"
#define RADIO_TX_MEM              4096
#define RADIO_TX_PRIORITY         5

//                                                              MQTT telemetry task
#define MQTT_TELEMETRY_TASK_NAME  "mqtt"
#define MQTT_TELEMETRY_MEM        4096
#define MQTT_TELEMETRY_PRIORITY   3

#define TELEMETRY_FREQ_HZ         1
#define TELEMETRY_PERIOD_MS       (1.0 / (TELEMETRY_FREQ_HZ) * 1000.0)

// How many samples to collect before logging stats
#define MQTT_STATS_REPORT_INTERVAL 40


// ------------------------------------------------------------ Packet/versioning
#define VERSION 1
#define TEAM_ID 1

#define MAX_JSON_STRING_LENGTH 1024


