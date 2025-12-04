// main/comms_mqtt.c

#include "tasks.h"
#include "config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;

// -----------------------------------------------------------------------------
// MQTT event handler
// -----------------------------------------------------------------------------

static void mqtt_event_handler(void* handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void* event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {

    case MQTT_EVENT_CONNECTED:
        fast_log("MQTT (I): connected to broker");
        s_connected = true;

        // Subscribe to test/command topic (like COMP0221/test)
        if (strlen(MQTT_TOPIC) > 0) {
            int msg_id = esp_mqtt_client_subscribe(event->client,
                                                   MQTT_TOPIC, 1);
            fast_log("MQTT (I): subscribed to %s (msg_id=%d)",
                     MQTT_TOPIC, msg_id);
        }
        break;

    // case MQTT_EVENT_DATA:
    //     // Log received MQTT messages (like the example)
    //     fast_log("MQTT (I): RX topic=%.*s payload=%.*s",
    //              event->topic_len, event->topic,
    //              event->data_len, event->data);
    //     break;

    case MQTT_EVENT_DISCONNECTED:
        fast_log("MQTT (W): disconnected from broker");
        s_connected = false;
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// JSON telemetry builder (same as before)
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// JSON telemetry builder
// -----------------------------------------------------------------------------

static void make_json(DroneState *s, char *buf, size_t buf_size)
{
    static uint16_t seq = 0;

    // Convert internal state to packed neighbour state
    NeighbourState p = DroneState_to_NeighbourState(s, seq++);
    
    // Sign the packet to generate the HMAC (mac_tag)
    sign_packet(&p);

    // Create the hex string for the mac_tag (4 bytes -> 8 hex chars)
    char mac_str[9];
    snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X",
             p.mac_tag[0], p.mac_tag[1], p.mac_tag[2], p.mac_tag[3]);

    // Build the JSON string
    // Note: Coordinates and velocities are cast to (int) and use %d 
    // to correctly display negative values.
    snprintf(buf, buf_size,
             "{"
             "\"version\":%u,"
             "\"team_id\":%u,"
             "\"node_id\":\"%02X%02X%02X%02X%02X%02X\","
             "\"seq_number\":%u,"
             "\"ts_s\":%lu,"
             "\"ts_ms\":%u,"
             "\"x_mm\":%d,"
             "\"y_mm\":%d,"
             "\"z_mm\":%d,"
             "\"vx_mm_s\":%d,"
             "\"vy_mm_s\":%d,"
             "\"vz_mm_s\":%d,"
             "\"yaw_cd\":%u,"
             "\"mac_tag\":\"%s\""
             "}",
             p.version,
             p.team_id,
             p.node_id[0], p.node_id[1], p.node_id[2],
             p.node_id[3], p.node_id[4], p.node_id[5],
             p.seq_number,
             (unsigned long)p.ts_s,
             p.ts_ms,
             (int)p.x_mm,      // Signed
             (int)p.y_mm,      // Signed
             (int)p.z_mm,      // Signed
             (int)p.vx_mm_s,   // Signed
             (int)p.vy_mm_s,   // Signed
             (int)p.vz_mm_s,   // Signed
             (unsigned)p.yaw_cd,
             mac_str);
}

// -----------------------------------------------------------------------------
// Telemetry task (publishes to MQTT_TOPIC)
// -----------------------------------------------------------------------------

static void mqtt_task(void *arg)
{
    (void)arg;

    QueueHandle_t telem_q = get_telemetry_state_queue();

    TickType_t period = pdMS_TO_TICKS(MQTT_TELEMETRY_PERIOD_MS);
    TickType_t next   = xTaskGetTickCount();

    DroneState s;

    while (true) {
        vTaskDelayUntil(&next, period);

        if (!s_connected) {
            continue;
        }

        if (xQueueReceive(telem_q, &s, 0) == pdTRUE) {
            char buf[MAX_JSON_STRING_LENGTH];
            make_json(&s, buf, sizeof(buf));

            int msg_id = esp_mqtt_client_publish(
                s_client, MQTT_TOPIC, buf, 0, 1, 0);

            if (msg_id == -1) {
                fast_log("MQTT (E): publish failed");
            // } else {
            //     fast_log("MQTT (I): published telemetry (msg_id=%d)", msg_id);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------

void init_mqtt_telemetry(void)
{
    // Matches the demo: broker.address.uri = MQTT_BROKER_URI
    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = MQTT_BROKER_URI,
            },
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        fast_log("MQTT (F): client init failed");
        return;
    }

    esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_mqtt_client_start(s_client);

    xTaskCreate(mqtt_task,
                MQTT_TELEMETRY_TASK_NAME,
                MQTT_TELEMETRY_MEM,
                NULL,
                MQTT_TELEMETRY_PRIORITY,
                NULL);
}