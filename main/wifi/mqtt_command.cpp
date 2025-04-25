#include "settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <thread_util.h>

#include <esp_matter_controller_utils.h>
#include <esp_matter_client.h>
#include <esp_matter_controller_client.h>

#include "matter_callbacks.h"

using namespace esp_matter;
using namespace esp_matter::controller;
using namespace chip;
using namespace chip::app::Clusters;
// using esp_matter::controller::device_mgr::endpoint_entry_t;
// using esp_matter::controller::device_mgr::matter_device_t;

static const char *TAG = "MQTT";

void hex_string_to_bytes(const char *hex_string, uint8_t *byte_array, size_t byte_array_len)
{
    size_t hex_len = strlen(hex_string);
    for (size_t i = 0; i < hex_len / 2 && i < byte_array_len; i++)
    {
        sscanf(&hex_string[i * 2], "%2hhx", &byte_array[i]);
    }
}

// Callback-функция для обработки отчетов атрибутов
/*
void OnAttributeData(uint64_t node_id,
                     const chip::app::ConcreteDataAttributePath &path,
                     chip::TLV::TLVReader *data)
{
    // Обработка данных атрибута
    ESP_LOGI("MATTER", "Received attribute report from node 0x%" PRIx64, node_id);
    ESP_LOGI("MATTER", "Endpoint: %u, Cluster: 0x%" PRIx32 ", Attribute: 0x%" PRIx32,
             path.mEndpointId, path.mClusterId, path.mAttributeId);

    // Декодирование данных...
    if (data != nullptr)
    {
        // Ваш код для обработки данных
    }
}
*/
extern "C" void handle_mqtt_data(esp_mqtt_event_handle_t event)
{
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");

    // Extract topic and data
    char topic[event->topic_len + 1];
    memcpy(topic, event->topic, event->topic_len);
    topic[event->topic_len] = '\0';

    char data[event->data_len + 1];
    memcpy(data, event->data, event->data_len);
    data[event->data_len] = '\0';

    ESP_LOGI(TAG, "TOPIC=%s", topic);
    ESP_LOGI(TAG, "DATA=%s", data);

    // Check for Matter command topic
    if (strstr(topic, "/command/matter") != NULL)
    {
        cJSON *json = cJSON_Parse(data);
        if (json == NULL)
        {
            ESP_LOGE(TAG, "Invalid JSON received");
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL)
            {
                ESP_LOGE(TAG, "JSON error before: %s", error_ptr);
            }
            return;
        }

        // Process "actions" field
        cJSON *actions = cJSON_GetObjectItem(json, "actions");
        if (actions && cJSON_IsString(actions))
        {
            const char *action_str = actions->valuestring;

            if (strcmp(action_str, "reboot") == 0)
            {
                ESP_LOGW(TAG, "Reboot ESP");
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                esp_restart();
            }
            else if (strcmp(action_str, "factoryreset") == 0)
            {
                ESP_LOGW(TAG, "Matter factory reset");
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                esp_matter::factory_reset();
            }
            else if (strcmp(action_str, "initOpenThread") == 0)
            {
                const char *mqttPrefix = sys_settings.mqtt.prefix;
                const char *topic = "/event/matter/";
                char eventTopic[strlen(mqttPrefix) + strlen(topic) + 1];
                strcpy(eventTopic, mqttPrefix);
                strcat(eventTopic, topic);

                ESP_LOGW(TAG, "Matter init OpenThread");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                const char *dataset = thread_dataset_init_new();
                if (dataset)
                {
                    strcpy(sys_settings.thread.TLVs, dataset);
                    // TODO: Publish success message
                    // esp_mqtt_client_publish(client, eventTopic, "{\"event\":\"initOpenThreadSuccess\"}", 0, 0, 0);
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to generate dataset TLVs.");
                    strcpy(sys_settings.thread.TLVs, "");
                    // TODO: Publish failure message
                    // esp_mqtt_client_publish(client, eventTopic, "{\"event\":\"initOpenThreadFail\"}", 0, 0, 0);
                }
                settings_save_to_nvs();
                ifconfig_up();
                thread_start();
            }
            else if (strcmp(action_str, "pairing") == 0)
            {
                ESP_LOGW(TAG, "Pairing command");
                cJSON *node = cJSON_GetObjectItem(json, "node");
                if (node && cJSON_IsNumber(node))
                {
                    uint64_t node_id = (uint64_t)cJSON_GetNumberValue(node);
                    ESP_LOGW(TAG, "Pairing with node ID: %llu", node_id);
                    cJSON *method = cJSON_GetObjectItem(json, "method");
                    if (method && cJSON_IsString(method))
                    {
                        const char *method_str = method->valuestring;
                        ESP_LOGW(TAG, "Pairing method: %s", method_str);
                        if (strcmp(method_str, "ble-wifi") == 0)
                        {
                            cJSON *pincode = cJSON_GetObjectItem(json, "pincode");
                            if (!pincode || !cJSON_IsNumber(pincode))
                            {
                                ESP_LOGE(TAG, "Missing or invalid pincode");
                                cJSON_Delete(json);
                                return;
                            }
                            uint32_t pincode_id = (uint32_t)cJSON_GetNumberValue(pincode);
                            cJSON *disc = cJSON_GetObjectItem(json, "discriminator");
                            if (!disc || !cJSON_IsNumber(disc))
                            {
                                ESP_LOGE(TAG, "Missing or invalid discriminator");
                                cJSON_Delete(json);
                                return;
                            }
                            uint16_t disc_id = (uint16_t)cJSON_GetNumberValue(disc);
                            cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
                            if (!ssid || !cJSON_IsString(ssid))
                            {
                                ESP_LOGE(TAG, "Missing or invalid ssid");
                                cJSON_Delete(json);
                                return;
                            }
                            const char *ssid_str = ssid->valuestring;
                            cJSON *pwd = cJSON_GetObjectItem(json, "pwd");
                            if (!pwd || !cJSON_IsString(pwd))
                            {
                                ESP_LOGE(TAG, "Missing or invalid password");
                                cJSON_Delete(json);
                                return;
                            }
                            const char *pwd_str = pwd->valuestring;
                            esp_matter::controller::pairing_ble_wifi(node_id, pincode_id, disc_id, ssid_str, pwd_str);
                        }
                        else if (strcmp(method_str, "ble-thread") == 0)
                        {
                            cJSON *pincode = cJSON_GetObjectItem(json, "pincode");
                            if (!pincode || !cJSON_IsNumber(pincode))
                            {
                                ESP_LOGE(TAG, "Missing or invalid pincode");
                                cJSON_Delete(json);
                                return;
                            }
                            uint32_t pincode_id = (uint32_t)cJSON_GetNumberValue(pincode);
                            cJSON *disc = cJSON_GetObjectItem(json, "discriminator");
                            if (!disc || !cJSON_IsNumber(disc))
                            {
                                ESP_LOGE(TAG, "Missing or invalid discriminator");
                                cJSON_Delete(json);
                                return;
                            }
                            uint16_t disc_id = (uint16_t)cJSON_GetNumberValue(disc);
                            const char *dataset = sys_settings.thread.TLVs;
                            size_t dataset_len = strlen(dataset) / 2;
                            uint8_t dataset_tlvs[dataset_len];
                            hex_string_to_bytes(dataset, dataset_tlvs, dataset_len);
                            esp_matter::controller::pairing_ble_thread(node_id, pincode_id, disc_id, dataset_tlvs, dataset_len);
                        }
                    }
                }
            }
            else if (strcmp(action_str, "subs-attr") == 0)
            {
                ESP_LOGW(TAG, "Subscribe command");
                cJSON *node = cJSON_GetObjectItem(json, "node");
                cJSON *endpoint = cJSON_GetObjectItem(json, "endpoint");
                cJSON *cluster = cJSON_GetObjectItem(json, "cluster");
                cJSON *attr = cJSON_GetObjectItem(json, "attr");
                cJSON *min_interval = cJSON_GetObjectItem(json, "min_interval");
                cJSON *max_interval = cJSON_GetObjectItem(json, "max_interval");
                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) &&
                    cluster && cJSON_IsNumber(cluster) && attr && cJSON_IsNumber(attr) &&
                    min_interval && cJSON_IsNumber(min_interval) && max_interval && cJSON_IsNumber(max_interval))
                {
                    uint64_t node_id = (uint64_t)cJSON_GetNumberValue(node);
                    uint16_t endpoint_id = (uint16_t)cJSON_GetNumberValue(endpoint);
                    uint32_t cluster_id = (uint32_t)cJSON_GetNumberValue(cluster);
                    uint32_t attr_id = (uint32_t)cJSON_GetNumberValue(attr);
                    uint16_t min_interval_id = (uint16_t)cJSON_GetNumberValue(min_interval);
                    uint16_t max_interval_id = (uint16_t)cJSON_GetNumberValue(max_interval);

                    ESP_LOGW(TAG, "Subscribing to node ID: %llu, endpoint ID: %u, cluster ID: %u, attr ID: %u", node_id, endpoint_id, cluster_id, attr_id);

                    subscribe_command *cmd = chip::Platform::New<subscribe_command>(node_id, endpoint_id, cluster_id, attr_id, SUBSCRIBE_ATTRIBUTE, min_interval_id, max_interval_id, true, OnAttributeData, nullptr, nullptr, nullptr);
                    if (!cmd)
                    {
                        ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
                    }
                    else
                    {
                        chip::DeviceLayer::PlatformMgr().LockChipStack();
                        cmd->send_command();
                        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
                    }
                 
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid parameters for subs-attr command");
                }
            }
            else
            {
                ESP_LOGW(TAG, "Unknown action: %s", action_str);
            }
        }
        else
        {
            ESP_LOGE(TAG, "No valid 'actions' field in JSON");
        }

        cJSON_Delete(json);
    }
}