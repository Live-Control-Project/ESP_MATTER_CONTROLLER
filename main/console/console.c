#include "console.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "../wifi/settings.h"
#include "../wifi/wifi.h"
#include "../wifi/mqtt.h"

static const char *TAG = "console";

/* Структуры для аргументов команд */
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} wifi_args;

static struct {
    struct arg_str *uri;
    struct arg_str *username;
    struct arg_str *password;
    struct arg_str *prefix;
    struct arg_end *end;
} mqtt_args;

static esp_mqtt_client_handle_t mqtt_client = NULL;

/* Функция для подключения к Wi-Fi */
static int wifi_connect(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&wifi_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_args.end, argv[0]);
        return 1;
    }


   memcpy(sys_settings.wifi.sta.ssid, wifi_args.ssid->sval[0], sizeof(sys_settings.wifi.sta.ssid));
   memcpy(sys_settings.wifi.sta.password, wifi_args.password->sval[0], sizeof(sys_settings.wifi.sta.password));
   sys_settings.wifi.mode = WIFI_MODE_STA;

esp_err_t ret = ESP_OK;
ret = settings_save_to_nvs();
if (ret != ESP_OK)
{
    ESP_LOGE("SETTINGS", "Save failed: %s", esp_err_to_name(ret));
}
else
{
    ESP_LOGI("SETTINGS", "Settings saved successfully!");
}
    ESP_LOGI(TAG, "Подключаемся к Wi-Fi SSID:%s", sys_settings.wifi.sta.ssid);
  
//    ESP_LOGW(TAG, "Reboot ESP");
//    vTaskDelay(3000 / portTICK_PERIOD_MS);
//    esp_restart();

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy(
        (char *)wifi_config.sta.ssid, 
        (const char *)sys_settings.wifi.sta.ssid, 
        sizeof(wifi_config.sta.ssid) - 1
    );
    
    strncpy(
        (char *)wifi_config.sta.password, 
        (const char *)sys_settings.wifi.sta.password, 
        sizeof(wifi_config.sta.password) - 1
    );
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());


    return 0;
}

static int mqtt_connect(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&mqtt_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqtt_args.end, argv[0]);
        return 1;
    }

    // Безопасное копирование строковых параметров
    if (mqtt_args.uri->count > 0) {
        strncpy(sys_settings.mqtt.server, mqtt_args.uri->sval[0], sizeof(sys_settings.mqtt.server) - 1);
        sys_settings.mqtt.server[sizeof(sys_settings.mqtt.server) - 1] = '\0';
    }
    
    if (mqtt_args.username->count > 0) {
        strncpy(sys_settings.mqtt.user, mqtt_args.username->sval[0], sizeof(sys_settings.mqtt.user) - 1);
        sys_settings.mqtt.user[sizeof(sys_settings.mqtt.user) - 1] = '\0';
    }
    
    if (mqtt_args.password->count > 0) {
        strncpy(sys_settings.mqtt.password, mqtt_args.password->sval[0], sizeof(sys_settings.mqtt.password) - 1);
        sys_settings.mqtt.password[sizeof(sys_settings.mqtt.password) - 1] = '\0';
    }
    
    if (mqtt_args.prefix->count > 0) {
        strncpy(sys_settings.mqtt.prefix, mqtt_args.prefix->sval[0], sizeof(sys_settings.mqtt.prefix) - 1);
        sys_settings.mqtt.prefix[sizeof(sys_settings.mqtt.prefix) - 1] = '\0';
    }

    // Сохранение настроек
    esp_err_t ret = settings_save_to_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(ret));
        return 1;
    }
    
    ESP_LOGI(TAG, "Settings saved successfully!");

    // Переподключение MQTT
    if (mqtt_client) {
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }

    ESP_LOGI(TAG, "Подключаемся к MQTT брокеру %s", sys_settings.mqtt.server);
    init_wifi_mqtt_handler();
    
    return 0;
}

/* Регистрация команд */
static void register_wifi_connect(void) {
    wifi_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID точки доступа");
    wifi_args.password = arg_str1(NULL, NULL, "<password>", "Пароль точки доступа");
    wifi_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "wifi",
        .help = "Connect to Wi-Fi",
        .hint = NULL,
        .func = &wifi_connect,
        .argtable = &wifi_args
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

static void register_mqtt_connect(void) {
    mqtt_args.uri = arg_str1(NULL, NULL, "<uri>", "URI MQTT брокера");
    mqtt_args.prefix = arg_str0(NULL, NULL, "<prefix>", "ID клиента");
    mqtt_args.username = arg_str0("u", "username", "<username>", "Имя пользователя");
    mqtt_args.password = arg_str0("p", "password", "<password>", "Пароль");
    mqtt_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "mqtt",
        .help = "Connect to MQTT",
        .hint = NULL,
        .func = &mqtt_connect,
        .argtable = &mqtt_args
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void console_init(void) {
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    /* Регистрируем команды */
    register_wifi_connect();
    register_mqtt_connect();

    ESP_LOGI(TAG, "Консольные команды инициализированы");
}