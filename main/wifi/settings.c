#include "settings.h"
#include <string.h>

system_settings_t sys_settings;

// Ключ для хранения в NVS
#define NVS_NAMESPACE "sys_settings"
#define NVS_KEY "settings"

void settings_set_defaults() {
    memset(&sys_settings, 0, sizeof(sys_settings));

    // Device settings
    strncpy(sys_settings.device.devicename, "MatterController", sizeof(sys_settings.device.devicename));

    // WiFi Defaults
    sys_settings.wifi.mode = WIFI_MODE_AP;
    sys_settings.wifi.wifi_present = true;
    sys_settings.wifi.wifi_enabled = true;
    sys_settings.wifi.wifi_connected = false;
    sys_settings.wifi.STA_connected = false;
    strncpy(sys_settings.wifi.STA_MAC, "00:00:00:00:00:00", sizeof(sys_settings.wifi.STA_MAC));

    // IP Settings
    sys_settings.wifi.ip.dhcp = DEFAULT_WIFI_DHCP;
    strncpy(sys_settings.wifi.ip.ip, "192.168.4.1", sizeof(sys_settings.wifi.ip.ip));
    strncpy(sys_settings.wifi.ip.netmask, "255.255.255.0", sizeof(sys_settings.wifi.ip.netmask));
    strncpy(sys_settings.wifi.ip.gateway, "192.168.4.1", sizeof(sys_settings.wifi.ip.gateway));
    strncpy(sys_settings.wifi.ip.dns, "8.8.8.8", sizeof(sys_settings.wifi.ip.dns));

    // AP Settings
    memset(&sys_settings.wifi.ap, 0, sizeof(wifi_ap_config_t));
    memcpy(sys_settings.wifi.ap.ssid, "MatterController", sizeof("MatterController"));
    sys_settings.wifi.ap.channel = DEFAULT_WIFI_AP_CHANNEL;
    memcpy(sys_settings.wifi.ap.password, "", sizeof(""));
    sys_settings.wifi.ap.max_connection = DEFAULT_WIFI_AP_MAXCONN;
    sys_settings.wifi.ap.authmode = DEFAULT_WIFI_AP_AUTHMODE;
    sys_settings.wifi.ap.ssid_hidden = 0;
    sys_settings.wifi.ap.beacon_interval = 100;

    // STA Settings
    memset(&sys_settings.wifi.sta, 0, sizeof(wifi_sta_config_t));
    memcpy(sys_settings.wifi.sta.ssid, "MyWiFi", sizeof("MyWiFi"));
    memcpy(sys_settings.wifi.sta.password, "MyPassword", sizeof("MyPassword"));
    sys_settings.wifi.sta.threshold.authmode = DEFAULT_WIFI_STA_AUTHMODE;
    sys_settings.wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sys_settings.wifi.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sys_settings.wifi.sta.threshold.rssi = -127;
    sys_settings.wifi.sta.pmf_cfg.capable = true;
    sys_settings.wifi.sta.pmf_cfg.required = false;

    // MQTT Settings
    sys_settings.mqtt.mqtt_enabled = false;
    sys_settings.mqtt.mqtt_connected = false;
    strncpy(sys_settings.mqtt.server, "mqtt.eclipse.org", sizeof(sys_settings.mqtt.server));
    sys_settings.mqtt.port = 1883;
    strncpy(sys_settings.mqtt.prefix, "home/device1", sizeof(sys_settings.mqtt.prefix));
    strncpy(sys_settings.mqtt.user, "", sizeof(sys_settings.mqtt.user));
    strncpy(sys_settings.mqtt.password, "", sizeof(sys_settings.mqtt.password));
    strncpy(sys_settings.mqtt.path, "", sizeof(sys_settings.mqtt.path));
}

// Сохранение настроек в NVS
esp_err_t settings_save_to_nvs() {
    nvs_handle_t handle;
    esp_err_t err;

    // Открываем NVS namespace в режиме записи
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    // Записываем всю структуру как бинарный blob
    err = nvs_set_blob(handle, NVS_KEY, &sys_settings, sizeof(sys_settings));
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    // Фиксируем изменения
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

// Загрузка настроек из NVS
esp_err_t settings_load_from_nvs() {
    nvs_handle_t handle;
    esp_err_t err;

    // Открываем NVS namespace в режиме чтения
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    // Получаем размер данных
    size_t required_size = sizeof(sys_settings);
    err = nvs_get_blob(handle, NVS_KEY, &sys_settings, &required_size);

    nvs_close(handle);

    // Если данные не найдены или размер не совпадает, устанавливаем значения по умолчанию
    if (err == ESP_ERR_NVS_NOT_FOUND || required_size != sizeof(sys_settings)) {
        settings_set_defaults();
        return ESP_ERR_NVS_NOT_FOUND;
    }

    return err;
}