#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_wifi_types.h"
#include "nvs_flash.h"
#include "nvs.h"

// Дефолтные константы (добавлены все недостающие)
#define DEFAULT_WIFI_MODE WIFI_MODE_STA
#define DEFAULT_WIFI_DHCP true
#define DEFAULT_WIFI_AP_MAXCONN 4
#define DEFAULT_WIFI_AP_CHANNEL 6
#define DEFAULT_WIFI_AP_AUTHMODE WIFI_AUTH_WPA_WPA2_PSK
#define DEFAULT_WIFI_STA_AUTHMODE WIFI_AUTH_WPA2_PSK

typedef struct {
    struct {
        char devicename[16];
    } device;

    struct {
        uint32_t mode;
        bool wifi_present;
        bool wifi_enabled;
        bool wifi_connected;
        bool STA_connected;
        char STA_MAC[18];
        struct {
            bool dhcp;
            char ip[16];
            char netmask[16];
            char gateway[16];
            char dns[16];
        } ip;
        wifi_ap_config_t ap;
        wifi_sta_config_t sta;
    } wifi;

    struct {
        bool mqtt_enabled;
        bool mqtt_connected;
        char server[32];
        int port;
        char prefix[64];
        char user[32];
        char password[32];
        char path[64];
    } mqtt;
} system_settings_t;

extern system_settings_t sys_settings;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t settings_save_to_nvs();
esp_err_t settings_load_from_nvs();
void settings_set_defaults();

#ifdef __cplusplus
}
#endif