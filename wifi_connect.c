// main/wifi_connect.c
#include "tasks.h"
#include "config.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"   // for eduroam / WPA2 Enterprise
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include <string.h>

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT      = BIT1;

static int s_retry_num = 0;

static void wifi_event_handler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {

        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        if (s_retry_num < 15) {
            s_retry_num++;
            fast_log("WIFI (W): disconnected, retrying... (%d/15)", s_retry_num);
            esp_wifi_connect();
        } else {
            fast_log("WIFI (E): failed to connect after 3 retries");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        fast_log("WIFI (I): got IP");
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_connect(void)
{
    esp_err_t err;

    // -------- NVS --------
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        fast_log("WIFI (E): nvs_flash_init failed (%d)", err);
        return err;
    }

    // -------- Netif + WiFi driver --------
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // -------- WiFi config --------
    wifi_config_t wifi_cfg = { 0 };

#if USE_EDUROAM
    // eduroam (WPA2-Enterprise)
    strncpy((char*)wifi_cfg.sta.ssid, WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
#else
    // Home Wi-Fi (WPA2-PSK)
    memcpy(wifi_cfg.sta.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(wifi_cfg.sta.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
#endif

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

#if USE_EDUROAM
    // -------- WPA2-Enterprise (eduroam) credentials --------
    ESP_ERROR_CHECK(esp_eap_client_set_identity(
        (const uint8_t*)EDUROAM_IDENTITY,
        strlen(EDUROAM_IDENTITY)));
    ESP_ERROR_CHECK(esp_eap_client_set_username(
        (const uint8_t*)EDUROAM_USERNAME,
        strlen(EDUROAM_USERNAME)));
    ESP_ERROR_CHECK(esp_eap_client_set_password(
        (const uint8_t*)EDUROAM_PASSWORD,
        strlen(EDUROAM_PASSWORD)));

    // Enable EAP for station mode (from esp-idf docs)
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
#endif

    ESP_ERROR_CHECK(esp_wifi_start());

#if USE_EDUROAM
    fast_log("WIFI (I): connecting to eduroam as %s", EDUROAM_USERNAME);
#else
    fast_log("WIFI (I): connecting to %s...", WIFI_SSID);
#endif

    // Wait for either CONNECTED or FAIL, with overall timeout
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,    // don't clear bits
        pdFALSE,    // wait for ANY bit
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        fast_log("WIFI (I): connected");
        return ESP_OK;
    }

    fast_log("WIFI (E): giving up on Wi-Fi (timeout or too many retries)");
    return ESP_FAIL;
}