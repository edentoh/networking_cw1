// main/comms_lora.cpp
#include "EspHal.h"
#include "RadioLib.h"

extern "C" {
#include "config.h"
#include "tasks.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
}

static constexpr int PIN_SPI_SCK   = 5;
static constexpr int PIN_SPI_MISO  = 19;
static constexpr int PIN_SPI_MOSI  = 27;

static constexpr int PIN_LORA_CS   = 18;
static constexpr int PIN_LORA_RST  = 23;
static constexpr int PIN_LORA_DIO0 = 26;
static constexpr int PIN_LORA_DIO1 = 33;

// Radio params (match coursework defaults)
static constexpr float   LORA_FREQ_MHZ = 868.1f;
static constexpr float   LORA_BW_KHZ   = 250.0f;
static constexpr uint8_t LORA_SF       = 9;
static constexpr uint8_t LORA_CR       = 7;
static constexpr uint8_t LORA_SYNCWORD = 0x12;
static constexpr uint16_t LORA_PREAMBLE = 10;
static constexpr int8_t  LORA_POWER_DBM = 14;
static constexpr bool    LORA_CRC_ON    = true;

static EspHal hal(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);
static SX1276 lora(new Module(&hal, PIN_LORA_CS, PIN_LORA_DIO0,
                              PIN_LORA_RST, PIN_LORA_DIO1));

static SemaphoreHandle_t RX_SEM = nullptr;
static uint16_t PACKET_SEQ = 0;

extern "C" void IRAM_ATTR give_rx_semaphore(void)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(RX_SEM, &hp);
    if (hp) {
        portYIELD_FROM_ISR();
    }
}

static void radio_task(void *arg)
{
    (void)arg;

    QueueHandle_t state_q   = get_radio_state_queue();
    QueueHandle_t neigh_q   = get_neighbour_update_queue();

    DroneState self{};
    // Wait for first state
    xQueueReceive(state_q, &self, portMAX_DELAY);

    const TickType_t tx_period = pdMS_TO_TICKS(RADIO_TX_PERIOD_MS);
    TickType_t next_tx = xTaskGetTickCount() + tx_period;

    // Start in RX mode
    lora.startReceive();

    while (true) {
        // --------- WAIT FOR RX OR NEXT TX TIME ---------
        TickType_t now = xTaskGetTickCount();
        TickType_t wait_ticks = (next_tx > now) ? (next_tx - now) : 0;

        // Block until we either get an RX interrupt or it's time to TX
        if (xSemaphoreTake(RX_SEM, wait_ticks) == pdTRUE) {
            // ---------- HANDLE RX ----------
            NeighbourState rx{};
            int16_t r = lora.readData((uint8_t*)&rx, sizeof(rx));

            if (r == RADIOLIB_ERR_NONE) {
                if (verify_packet(&rx)) {
                    // ignore our own packets (we're not our own neighbour)
                    if (memcmp(rx.node_id, get_mac_address(), 6) == 0) {
                        //fast_log("RADIO (I): RX own packet (ignored) node=%s",
                                 //get_mac_address_string());
                    } else {
                        log_radio_packet("RX", &rx);
                        xQueueSend(neigh_q, &rx, 0);
                    }
                } else {
                    fast_log("RADIO (W): bad MAC from %s",
                             format_mac(rx.node_id));
                }
            } else {
                fast_log("RADIO (W): readData error (%d)", r);
            }

            // Go back to RX mode for further packets
            lora.startReceive();
            // loop back; TX will be handled when timeout hits
            continue;
        }

        // --------- TIMEOUT: DO TX NOW ---------
        now = xTaskGetTickCount();
        if ((TickType_t)(now - next_tx) < (TickType_t)0) {
            // Spurious wake; it's not TX time yet
            continue;
        }

        // Refresh latest state if available
        xQueueReceive(state_q, &self, 0);

        NeighbourState tx = DroneState_to_NeighbourState(&self, PACKET_SEQ++);
        sign_packet(&tx);

        int16_t res = lora.transmit((uint8_t*)&tx, sizeof(tx));
        if (res == RADIOLIB_ERR_NONE) {
            log_neighbour_state("RADIO TX", &tx);
        } else {
            fast_log("RADIO (E): TX failed (%d)", res);
        }

        // After TX, go back to RX immediately
        lora.startReceive();

        // Schedule next TX time
        next_tx = now + tx_period;
    }
}

extern "C" void init_radio(void)
{
    RX_SEM = xSemaphoreCreateBinary();
    if (!RX_SEM) {
        fast_log("RADIO (F): cannot create semaphore");
        vTaskDelay(portMAX_DELAY);
    }

    int16_t state = lora.begin(LORA_FREQ_MHZ,
                               LORA_BW_KHZ,
                               LORA_SF,
                               LORA_CR,
                               LORA_SYNCWORD,
                               LORA_PREAMBLE,
                               LORA_POWER_DBM,
                               LORA_CRC_ON);
    if (state != RADIOLIB_ERR_NONE) {
        fast_log("RADIO (F): LoRa init failed (%d)", state);
        vTaskDelay(portMAX_DELAY);
    }

    lora.setOutputPower(LORA_POWER_DBM);
    lora.setDio0Action(give_rx_semaphore, RISING);

    xTaskCreate(radio_task,
                RADIO_COMBINED_TASK_NAME,   // or RADIO_TASK_NAME in your config
                RADIO_COMBINED_MEM,
                nullptr,
                RADIO_COMBINED_PRIORITY,
                nullptr);
}