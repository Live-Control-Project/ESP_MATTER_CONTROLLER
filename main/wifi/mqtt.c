#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/semphr.h"
//#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "wifi/mqtt_command.h"
#include "mqtt_command.h"
#include "settings.h"


static const char *TAG = "MQTT";
static const char *TAG_WIFI = "WIFI_MQTT";

esp_mqtt_client_handle_t client = NULL;
uint32_t MQTT_CONNEECTED = 0;
char mqtt_url[32];
char mqtt_login[32];
char mqtt_pwd[32];

const char *deviceName = "esp_matter_controller";

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}
static esp_mqtt_client_handle_t mqtt_client;

esp_err_t mqtt_publish_data(const char *topic, const char *data)
{
    if (!client || !sys_settings.mqtt.mqtt_connected) {
        ESP_LOGE("MQTT", "Client not ready (init: %d, connected: %d)", 
            client != NULL, sys_settings.mqtt.mqtt_connected);
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE("MQTT", "Publish failed (error %d)", msg_id);
        return ESP_FAIL;
    }
    
//    ESP_LOGI("MQTT", "Published to [%s]: %s", topic, data);
    return ESP_OK;
}

void *get_mqtt_client()
{
    return client;
}

// Публикация статуса датчика
/*
void publis_status_mqtt(const char *topic, int EP, const char *deviceData)
{
    if (client == NULL || !MQTT_CONNEECTED) return;

    if (EP > 1)
    {
        char completeTopic[150];
        snprintf(completeTopic, sizeof(completeTopic), "%s/%d", topic, EP);
        esp_mqtt_client_publish(client, topic, deviceData, 0, 0, 0);
        printf("completeTopic: %s\n", completeTopic);
    }
    else
    {
        esp_mqtt_client_publish(client, topic, deviceData, 0, 0, 0);
        printf("completeTopic: %s\n", topic);
        printf("deviceData: %s\n", deviceData);
    }
}
*/
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    int msg_id;

    // Выносим объявления переменных в начало функции
    const char *mqttPrefixIN = sys_settings.mqtt.prefix;
    const char *topicIN = "/td/matter/#";
    char completeTopicIN[strlen(mqttPrefixIN) + strlen(topicIN) + 1];
    strcpy(completeTopicIN, mqttPrefixIN);
    strcat(completeTopicIN, topicIN);
    
    size_t topic_len = strlen(mqttPrefixIN) + strlen("/command/matter") + 1;
    char commandTopic[topic_len];
    snprintf(commandTopic, topic_len, "%s/command/matter", mqttPrefixIN);

    // Объявляем переменные для MQTT_EVENT_CONNECTED заранее
    const char *mqttPrefix = sys_settings.mqtt.prefix;
    const char *topic = "/device/matter/";
    char completeTopiclwt[strlen(mqttPrefix) + strlen(topic) + strlen(deviceName) + 1];

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        MQTT_CONNEECTED = 1;
        msg_id = esp_mqtt_client_subscribe(client, completeTopicIN, 0);
        ESP_LOGI(TAG, "subscribe successful to %s, msg_id=%d", completeTopicIN, msg_id);
        msg_id = esp_mqtt_client_subscribe(client, commandTopic, 0);
        ESP_LOGI(TAG, "subscribe successful to %s, msg_id=%d", commandTopic, msg_id);
        sys_settings.mqtt.mqtt_connected = true;
        strcpy(completeTopiclwt, mqttPrefix);
        strcat(completeTopiclwt, topic);
        strcat(completeTopiclwt, deviceName);
        esp_mqtt_client_publish(client, completeTopiclwt, "{\"status\":\"online\"}", 0, 0, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        if (mqtt_client != NULL) {
            esp_mqtt_client_destroy(mqtt_client);
            mqtt_client = NULL;
        }
        MQTT_CONNEECTED = 0;
        sys_settings.mqtt.mqtt_connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        //ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
    //    ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
    
        handle_mqtt_data(event);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

esp_err_t mqtt_app_start(void)
{
    if (client != NULL) {
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = NULL;
    }

    // Публикуем доступность устройства
    const char *mqttPrefix = sys_settings.mqtt.prefix;
    const char *topic = "/device/matter/";
    
    char completeTopiclwt[strlen(mqttPrefix) + strlen(topic) + strlen(deviceName) + 1];
    strcpy(completeTopiclwt, mqttPrefix);
    strcat(completeTopiclwt, topic);
    strcat(completeTopiclwt, deviceName);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = sys_settings.mqtt.server,
        .credentials.username = sys_settings.mqtt.user,
        .credentials.authentication.password = sys_settings.mqtt.password,
        .session.last_will.msg = "{\"status\":\"offline\"}",
        .session.last_will.topic = completeTopiclwt,
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    return ESP_OK;
}



void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                       int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG_WIFI, "Wi-Fi disconnected, stopping MQTT client");
        if (client != NULL) {
            esp_mqtt_client_stop(client);
            esp_mqtt_client_destroy(client);
            client = NULL;
            MQTT_CONNEECTED = 0;
            sys_settings.mqtt.mqtt_connected = false;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG_WIFI, "Wi-Fi connected, starting MQTT client");
        mqtt_app_start();
    }
}

void Publisher_Task(void *params)
{
    while (true)
    {
        if (MQTT_CONNEECTED && client != NULL)
        {
         //   esp_mqtt_client_publish(client, "/topic/test3", "Hello World", 0, 0, 0);
        }
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
void init_wifi_mqtt_handler()
{
    if (strcmp(sys_settings.mqtt.server, "") != 0){
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    
    // Если Wi-Fi уже подключен, запускаем MQTT сразу
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        mqtt_app_start();
    //    xTaskCreate(Publisher_Task, "Publisher_Task", 1024 * 5, NULL, 5, NULL);
    }
}
}

