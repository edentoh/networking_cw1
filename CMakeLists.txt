idf_component_register(
    SRCS
        "main.cpp"
        "drone_state.c"
        "physics.c"
        "flocking.c"
        "comms_lora.cpp"
        "comms_mqtt.c"
        "logging.c"
        "security.c"
        "wifi_connect.c"
        "sntp_time.c"
        "globals.c"
    INCLUDE_DIRS "."
    REQUIRES jgromes__radiolib esp_driver_gpio esp_timer mqtt esp_wifi esp_netif nvs_flash esp_event wpa_supplicant
)
