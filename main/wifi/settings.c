#include "settings.h"
#include <string.h>
#include <esp_err.h>
#include <esp_log.h>

static const char *TAG = "WiFi";

system_settings_t sys_settings;

#define CHECK(x) do { \
    esp_err_t __err = (x); \
    if (__err != ESP_OK) return __err; \
} while (0)

#define CHECK_LOGE(x, msg, ...) do { \
    esp_err_t __err = (x); \
    if (__err != ESP_OK) { \
        ESP_LOGE(TAG, msg, ##__VA_ARGS__); \
        return __err; \
    } \
} while (0)

#define NVS_NAMESPACE "sys_settings"
#define NVS_KEY "settings"
static const char *OPT_MAGIC = "magic";
static const char *OPT_SETTINGS = "settings";
#define SETTINGS_MAGIC 0xbeef001a
/*
static void log_wifi_settings() {
    ESP_LOGI(TAG, "WiFi Settings:");
    ESP_LOGI(TAG, "  SSID: %s", sys_settings.wifi.ap.ssid);
    ESP_LOGI(TAG, "  Password: %s", sys_settings.wifi.ap.password);
    ESP_LOGI(TAG, "  Channel: %d", sys_settings.wifi.ap.channel);
    ESP_LOGI(TAG, "  Authmode: %d", sys_settings.wifi.ap.authmode);
}
*/
static void init_string_field(void* dest, const char* src, size_t size) {
    strlcpy((char*)dest, src, size);
}

void settings_set_defaults() {
    memset(&sys_settings, 0, sizeof(sys_settings));

    // Device settings
    init_string_field(sys_settings.device.devicename, "MatterController", sizeof(sys_settings.device.devicename));

    // WiFi Defaults
    sys_settings.wifi.mode = WIFI_MODE_AP;
    sys_settings.wifi.wifi_present = true;
    sys_settings.wifi.wifi_enabled = true;
    sys_settings.wifi.wifi_connected = false;
    sys_settings.wifi.STA_connected = false;
    init_string_field(sys_settings.wifi.STA_MAC, "00:00:00:00:00:00", sizeof(sys_settings.wifi.STA_MAC));

    // IP Settings
    sys_settings.wifi.ip.dhcp = DEFAULT_WIFI_DHCP;
    init_string_field(sys_settings.wifi.ip.ip, "192.168.4.1", sizeof(sys_settings.wifi.ip.ip));
    init_string_field(sys_settings.wifi.ip.netmask, "255.255.255.0", sizeof(sys_settings.wifi.ip.netmask));
    init_string_field(sys_settings.wifi.ip.gateway, "192.168.4.1", sizeof(sys_settings.wifi.ip.gateway));
    init_string_field(sys_settings.wifi.ip.dns, "8.8.8.8", sizeof(sys_settings.wifi.ip.dns));

    // AP Settings
    memset(&sys_settings.wifi.ap, 0, sizeof(wifi_ap_config_t));
    init_string_field(sys_settings.wifi.ap.ssid, "MatterController", sizeof(sys_settings.wifi.ap.ssid));
    sys_settings.wifi.ap.channel = DEFAULT_WIFI_AP_CHANNEL;
    init_string_field(sys_settings.wifi.ap.password, "", sizeof(sys_settings.wifi.ap.password));
    sys_settings.wifi.ap.max_connection = DEFAULT_WIFI_AP_MAXCONN;
    sys_settings.wifi.ap.authmode = DEFAULT_WIFI_AP_AUTHMODE;
    sys_settings.wifi.ap.ssid_hidden = 0;
    sys_settings.wifi.ap.beacon_interval = 100;

    // STA Settings
    memset(&sys_settings.wifi.sta, 0, sizeof(wifi_sta_config_t));
    init_string_field(sys_settings.wifi.sta.ssid, "MyWiFi", sizeof(sys_settings.wifi.sta.ssid));
    init_string_field(sys_settings.wifi.sta.password, "MyPassword", sizeof(sys_settings.wifi.sta.password));
    sys_settings.wifi.sta.threshold.authmode = DEFAULT_WIFI_STA_AUTHMODE;
    sys_settings.wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sys_settings.wifi.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sys_settings.wifi.sta.threshold.rssi = -127;
    sys_settings.wifi.sta.pmf_cfg.capable = true;
    sys_settings.wifi.sta.pmf_cfg.required = false;

    // MQTT Settings
    sys_settings.mqtt.mqtt_enabled = false;
    sys_settings.mqtt.mqtt_connected = false;
    init_string_field(sys_settings.mqtt.server, "", sizeof(sys_settings.mqtt.server));
    sys_settings.mqtt.port = 1883;
    init_string_field(sys_settings.mqtt.prefix, "", sizeof(sys_settings.mqtt.prefix));
    init_string_field(sys_settings.mqtt.user, "", sizeof(sys_settings.mqtt.user));
    init_string_field(sys_settings.mqtt.password, "", sizeof(sys_settings.mqtt.password));
    init_string_field(sys_settings.mqtt.path, "", sizeof(sys_settings.mqtt.path));
    
    ESP_LOGI(TAG, "Loaded default settings");
    //log_wifi_settings();

    settings_save_to_nvs();
}

esp_err_t settings_save_to_nvs() {
    ESP_LOGD(TAG, "Saving settings to NVS namespace '%s'", NVS_NAMESPACE);

    nvs_handle_t nvs;
    CHECK_LOGE(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs),
              "Failed to open NVS namespace for writing");
    
    CHECK_LOGE(nvs_set_u32(nvs, OPT_MAGIC, SETTINGS_MAGIC),
              "Failed to write magic number");
    
    CHECK_LOGE(nvs_set_blob(nvs, OPT_SETTINGS, &sys_settings, sizeof(sys_settings)),
              "Failed to write settings blob");
    
    nvs_commit(nvs);
    nvs_close(nvs);

    ESP_LOGD(TAG, "Settings successfully saved");
    return ESP_OK;
}

esp_err_t settings_load_from_nvs() {
    ESP_LOGD(TAG, "Loading settings from NVS namespace '%s'", NVS_NAMESPACE);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Settings not found, loading defaults");
        settings_set_defaults();
        return ESP_ERR_NVS_NOT_FOUND;
    }
    CHECK_LOGE(err, "Failed to open NVS namespace");

    // Check magic number
    uint32_t magic = 0;
    err = nvs_get_u32(nvs, OPT_MAGIC, &magic);
    if (err != ESP_OK || magic != SETTINGS_MAGIC) {
        nvs_close(nvs);
        ESP_LOGW(TAG, "Invalid magic number (got 0x%08lx, expected 0x%08x)", magic, SETTINGS_MAGIC);
        settings_set_defaults();
        return ESP_FAIL;
    }

    // Load settings blob
    size_t required_size = sizeof(sys_settings);
    err = nvs_get_blob(nvs, OPT_SETTINGS, &sys_settings, &required_size);
    nvs_close(nvs);

    if (err != ESP_OK || required_size != sizeof(sys_settings)) {
        ESP_LOGW(TAG, "Invalid settings size or read error");
        settings_set_defaults();
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Settings successfully loaded");
   // log_wifi_settings();
    return ESP_OK;
}