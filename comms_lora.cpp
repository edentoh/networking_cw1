// main/comms_lora.cpp
#include "EspHal.h"
#include "RadioLib.h"
#include "monitoring.h" // <--- Added

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

// Radio params
static constexpr float   LORA_FREQ_MHZ = 868.2f;
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

static volatile bool s_transmitting = false; 
static TickType_t last_tx_end_tick = 0; 

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
    QueueHandle_t attack_q  = get_attack_queue();

    DroneState self{};
    xQueueReceive(state_q, &self, portMAX_DELAY);

    const TickType_t tx_period = pdMS_TO_TICKS(RADIO_TX_PERIOD_MS);
    TickType_t next_tx = xTaskGetTickCount() + tx_period;

    lora.startReceive();
    s_transmitting = false;

    while (true) {
        
        // 1. ATTACK INJECTION
        NeighbourState attack_pkt;
        if (xQueueReceive(attack_q, &attack_pkt, 0) == pdTRUE) {
            int16_t res = lora.startTransmit((uint8_t*)&attack_pkt, sizeof(attack_pkt));
            if (res == RADIOLIB_ERR_NONE) {
                s_transmitting = true;
                monitor_radio_state(true, 50); // Log Energy
            }
            while(s_transmitting) {
                if (xSemaphoreTake(RX_SEM, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (s_transmitting) {
                         s_transmitting = false;
                         last_tx_end_tick = xTaskGetTickCount(); 
                         lora.startReceive();
                    }
                }
            }
            continue;
        }

        // 2. NORMAL RADIO LOOP
        TickType_t now = xTaskGetTickCount();
        TickType_t wait_ticks = (next_tx > now) ? (next_tx - now) : 0;

        // --- MONITOR START ---
        monitor_task_start(MON_TASK_RADIO);

        if (xSemaphoreTake(RX_SEM, wait_ticks) == pdTRUE) {
            
            if (s_transmitting) {
                // TX DONE
                s_transmitting = false;
                last_tx_end_tick = xTaskGetTickCount(); 
                lora.startReceive();
                
            } else {
                // RX DONE
                
                // Anti-Echo Check
                if (xTaskGetTickCount() - last_tx_end_tick < pdMS_TO_TICKS(50)) {
                    lora.startReceive();
                    monitor_task_end(MON_TASK_RADIO);
                    continue;
                }

                NeighbourState rx{};
                int16_t r = lora.readData((uint8_t*)&rx, sizeof(rx));

                if (r == RADIOLIB_ERR_NONE) {
                    
                    // Ignore Own MAC
                    if (memcmp(rx.node_id, get_mac_address(), 6) == 0) {
                        lora.startReceive();
                        monitor_task_end(MON_TASK_RADIO);
                        continue;
                    }

                    // Verify Crypto
                    if (!verify_packet(&rx)) {
                        uint8_t spoof_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
                        if (memcmp(rx.node_id, spoof_mac, 6) != 0) {
                             fast_log("RADIO (W): Bad MAC/Sig from %s", format_mac(rx.node_id));
                        }
                        lora.startReceive();
                        monitor_task_end(MON_TASK_RADIO);
                        continue;
                    }

                    // Security Logic (Rate Limit / Physics)
                    if (!security_validate_packet(&rx)) {
                        lora.startReceive();
                        monitor_task_end(MON_TASK_RADIO);
                        continue;
                    }

                    // Valid
                    log_radio_packet("RX", &rx);
                    xQueueSend(neigh_q, &rx, 0);

                } else if (r != RADIOLIB_ERR_CRC_MISMATCH) {
                    fast_log("RADIO (W): readData error (%d)", r);
                }

                lora.startReceive();
            }

        } else {
            
            // TIMEOUT -> NORMAL TX
            xQueueReceive(state_q, &self, 0);

            NeighbourState tx = DroneState_to_NeighbourState(&self, PACKET_SEQ++);
            sign_packet(&tx);

            int16_t res = lora.startTransmit((uint8_t*)&tx, sizeof(tx));
            if (res == RADIOLIB_ERR_NONE) {
                log_neighbour_state("RADIO TX ", &tx);
                s_transmitting = true;
                monitor_radio_state(true, 50); // Log Energy (approx 50ms)
            } else {
                fast_log("RADIO (E): StartTransmit failed (%d)", res);
                lora.startReceive();
                s_transmitting = false;
            }

            while (next_tx <= xTaskGetTickCount()) {
                next_tx += tx_period;
            }
        }

        // --- MONITOR END ---
        monitor_task_end(MON_TASK_RADIO);
    }
}

extern "C" void init_radio(void)
{
    RX_SEM = xSemaphoreCreateBinary();
    if (!RX_SEM) vTaskDelay(portMAX_DELAY);

    int16_t state = lora.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                               LORA_SYNCWORD, LORA_PREAMBLE, LORA_POWER_DBM, LORA_CRC_ON);
    
    if (state != RADIOLIB_ERR_NONE) {
        fast_log("RADIO (F): Init failed %d", state);
        vTaskDelay(portMAX_DELAY);
    }

    lora.setOutputPower(LORA_POWER_DBM);
    lora.setDio0Action(give_rx_semaphore, RISING);

    xTaskCreate(radio_task, RADIO_COMBINED_TASK_NAME, RADIO_COMBINED_MEM,
                nullptr, RADIO_COMBINED_PRIORITY, nullptr);
}