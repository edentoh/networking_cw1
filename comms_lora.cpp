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

// NEW: Track radio state to distinguish RX vs TX interrupts
static volatile bool s_transmitting = false; 

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
    // Wait for first state so we have something to send
    xQueueReceive(state_q, &self, portMAX_DELAY);

    const TickType_t tx_period = pdMS_TO_TICKS(RADIO_TX_PERIOD_MS);
    TickType_t next_tx = xTaskGetTickCount() + tx_period;

    // 1. Initial State: Always be listening
    lora.startReceive();
    s_transmitting = false;

    QueueHandle_t attack_q = get_attack_queue(); // <--- GET QUEUE

    while (true) {
        
        // ----------------------------------------------------------------
        // 1. CHECK FOR ATTACK PACKETS (High Priority Injection)
        // ----------------------------------------------------------------
        NeighbourState attack_pkt;
        if (xQueueReceive(attack_q, &attack_pkt, 0) == pdTRUE) {
            
            // Abort any RX, transmit immediately
            int16_t res = lora.startTransmit((uint8_t*)&attack_pkt, sizeof(attack_pkt));
            if (res == RADIOLIB_ERR_NONE) {
                // log_neighbour_state("ATTACK TX", &attack_pkt); // Optional log
                s_transmitting = true;
            }
            
            // Loop immediately to send next attack packet (don't wait for timeouts)
            // But we must wait for this specific TX to finish or we will overwrite it.
            // Simple approach: Wait for the TX_DONE interrupt here
            while(s_transmitting) {
                if (xSemaphoreTake(RX_SEM, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (s_transmitting) {
                         s_transmitting = false;
                         lora.startReceive(); // Back to RX
                    }
                }
            }
            continue; // Skip the normal RX/TX logic for this iteration
        }
        // Calculate how long to wait until the next scheduled TX
        TickType_t now = xTaskGetTickCount();
        TickType_t wait_ticks = (next_tx > now) ? (next_tx - now) : 0;

        // Block here until:
        // A) An interrupt occurs (RX packet OR TX finished)
        // B) The timeout expires (Time to send next packet)
        if (xSemaphoreTake(RX_SEM, wait_ticks) == pdTRUE) {
            
            // --- EVENT DETECTED (Interrupt) ---
            
            if (s_transmitting) {
                // CASE 1: We were transmitting, so this is a "TX Done" event
                s_transmitting = false;
                //fast_log("RADIO (I): TX complete");

                // CRITICAL: Switch immediately back to RX to catch incoming packets
                lora.startReceive();
                
            } else {
                // CASE 2: We were receiving, so this is an "RX Done" event
                NeighbourState rx{};
                // readData clears the IRQ flags automatically
                int16_t r = lora.readData((uint8_t*)&rx, sizeof(rx));

                if (r == RADIOLIB_ERR_NONE) {
                    if (verify_packet(&rx)) {
                        if (memcmp(rx.node_id, get_mac_address(), 6) != 0) {
                            log_radio_packet("RX", &rx);
                            xQueueSend(neigh_q, &rx, 0);
                        }
                    } else {
                        fast_log("RADIO (W): bad MAC from %s", format_mac(rx.node_id));
                    }
                } else if (r != RADIOLIB_ERR_CRC_MISMATCH) {
                     // CRC errors are common in LoRa, only log other weird errors
                    fast_log("RADIO (W): readData error (%d)", r);
                }

                // Resume RX immediately
                lora.startReceive();
            }

        } else {
            
            // --- TIMEOUT (Time to Transmit) ---

            // 1. Update latest state
            xQueueReceive(state_q, &self, 0);

            // 2. Prepare Packet
            NeighbourState tx = DroneState_to_NeighbourState(&self, PACKET_SEQ++);
            sign_packet(&tx);

            // 3. Start Non-Blocking Transmission
            // This aborts the current RX mode, switches to TX, and returns immediately.
            // When sending finishes, the interrupt will fire, and we handle it above.
            int16_t res = lora.startTransmit((uint8_t*)&tx, sizeof(tx));

            if (res == RADIOLIB_ERR_NONE) {
                log_neighbour_state("RADIO TX ", &tx);
                s_transmitting = true; // Mark that we are now busy sending
            } else {
                fast_log("RADIO (E): StartTransmit failed (%d)", res);
                // If TX failed to start, go back to RX immediately to not lose connectivity
                lora.startReceive();
                s_transmitting = false;
            }

            // 4. Schedule next TX
            // (Make sure we advance next_tx even if we missed the deadline slightly)
            while (next_tx <= xTaskGetTickCount()) {
                next_tx += tx_period;
            }
        }
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