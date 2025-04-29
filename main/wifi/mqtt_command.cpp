#include "settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt.h"

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_write_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_cluster_command.h>

#include <thread_util.h>

#include <esp_matter_controller_utils.h>
#include <esp_matter_client.h>
#include <esp_matter_controller_client.h>

#include "matter_callbacks.h"

using namespace esp_matter;
using namespace esp_matter::controller;
using namespace chip;
using namespace chip::app::Clusters;

static const char *TAG = "MQTT";

void hex_string_to_bytes(const char *hex_string, uint8_t *byte_array, size_t byte_array_len)
{
    size_t hex_len = strlen(hex_string);
    for (size_t i = 0; i < hex_len / 2 && i < byte_array_len; i++)
    {
        sscanf(&hex_string[i * 2], "%2hhx", &byte_array[i]);
    }
}

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

    //    ESP_LOGI(TAG, "TOPIC=%s", topic);
    //    ESP_LOGI(TAG, "DATA=%s", data);

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

        cJSON *actions = cJSON_GetObjectItem(json, "actions");
        cJSON *node = cJSON_GetObjectItem(json, "node");
        cJSON *endpoint = cJSON_GetObjectItem(json, "endpoint");
        cJSON *cluster = cJSON_GetObjectItem(json, "cluster");
        cJSON *attr = cJSON_GetObjectItem(json, "attr");
        cJSON *min_interval = cJSON_GetObjectItem(json, "min_interval");
        cJSON *max_interval = cJSON_GetObjectItem(json, "max_interval");
        cJSON *pincode = cJSON_GetObjectItem(json, "pincode");
        cJSON *disc = cJSON_GetObjectItem(json, "disc");
        cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
        cJSON *pwd = cJSON_GetObjectItem(json, "pwd");
        cJSON *payload = cJSON_GetObjectItem(json, "payload");
        cJSON *val = cJSON_GetObjectItem(json, "val");
        cJSON *attribute_val = cJSON_GetObjectItem(json, "attribute_val");

        uint64_t node_id = 0;
        uint16_t endpoint_id = 0, min_interval_id = 0, max_interval_id = 0, disc_id = 0;
        uint32_t cluster_id = 0, attr_id = 0, pincode_id = 0, command_id = 0;
        const char *ssid_str = "", *pwd_str = "", *payload_str = "";
        char *attribute_val_str = "";

        if (node && cJSON_IsNumber(node))
            node_id = (uint64_t)cJSON_GetNumberValue(node);
        if (endpoint && cJSON_IsNumber(endpoint))
            endpoint_id = (uint16_t)cJSON_GetNumberValue(endpoint);
        if (cluster && cJSON_IsNumber(cluster))
            cluster_id = (uint32_t)cJSON_GetNumberValue(cluster);
        if (attr && cJSON_IsNumber(attr))
            attr_id = (uint32_t)cJSON_GetNumberValue(attr);
        if (min_interval && cJSON_IsNumber(min_interval))
            min_interval_id = (uint16_t)cJSON_GetNumberValue(min_interval);
        if (max_interval && cJSON_IsNumber(max_interval))
            max_interval_id = (uint16_t)cJSON_GetNumberValue(max_interval);
        if (pincode && cJSON_IsNumber(pincode))
            pincode_id = (uint32_t)cJSON_GetNumberValue(pincode);
        if (disc && cJSON_IsNumber(disc))
            disc_id = (uint16_t)cJSON_GetNumberValue(disc);
        if (ssid && cJSON_IsString(ssid))
            ssid_str = ssid->valuestring;
        if (pwd && cJSON_IsNumber(pwd))
            pwd_str = pwd->valuestring;
        if (payload && cJSON_IsString(payload))
            payload_str = payload->valuestring;
        if (val && cJSON_IsNumber(val))
            command_id = (uint32_t)cJSON_GetNumberValue(val);
        if (attribute_val && cJSON_IsString(attribute_val))
            strcpy(attribute_val_str, attribute_val->valuestring);

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
                    esp_err_t ret = mqtt_publish_data(eventTopic, "{\"event\":\"initOpenThreadSuccess\"}");
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "MQTT publish failed with error: %s", esp_err_to_name(ret));
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to generate dataset TLVs.");
                    strcpy(sys_settings.thread.TLVs, "");
                    esp_err_t ret = mqtt_publish_data(eventTopic, "{\"event\":\"initOpenThreadFail\"}");
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "MQTT publish failed with error: %s", esp_err_to_name(ret));
                    }
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
                        // Pairing method onnetwork
                        if (strcmp(method_str, "onnetwork") == 0)
                        {
                            if (node && cJSON_IsNumber(node) && pincode && cJSON_IsNumber(pincode))
                                controller::pairing_on_network(node_id, pincode_id);
                        }
                        // Pairing method ble-wifi
                        else if (strcmp(method_str, "ble-wifi") == 0)
                        {
                            if (node && cJSON_IsNumber(node) && pincode && cJSON_IsNumber(pincode) && disc && cJSON_IsNumber(disc))
                                controller::pairing_ble_wifi(node_id, pincode_id, disc_id, ssid_str, pwd_str);
                            /*
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
                            */
                        }
                        // Pairing method ble-thread
                        else if (strcmp(method_str, "ble-thread") == 0)
                        {
                            /*
                            if (node && cJSON_IsNumber(node) && pincode && cJSON_IsNumber(pincode) && disc && cJSON_IsNumber(disc))
                            {
                                const char *dataset = sys_settings.thread.TLVs;
                                size_t dataset_len = strlen(dataset) / 2;
                                uint8_t dataset_tlvs[dataset_len];
                                hex_string_to_bytes(dataset, dataset_tlvs, dataset_len);
                                controller::pairing_ble_thread(node_id, pincode_id, disc_id, dataset_tlvs, dataset_len);
                            }
                                */

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
                        else if (strcmp(method_str, "code") == 0)
                        {
                            if (node && cJSON_IsNumber(node) && payload && cJSON_IsString(payload))
                            {
                                controller::pairing_code(node_id, payload_str);
                            }
                        }
                        else if (strcmp(method_str, "code-thread") == 0)
                        {
                            if (node && cJSON_IsNumber(node) && payload && cJSON_IsString(payload))
                            {
                                const char *dataset = sys_settings.thread.TLVs;
                                size_t dataset_len = strlen(dataset) / 2;
                                uint8_t dataset_tlvs[dataset_len];
                                hex_string_to_bytes(dataset, dataset_tlvs, dataset_len);
                                controller::pairing_code_thread(node_id, payload_str, dataset_tlvs, dataset_len);
                            }
                        }
                        else if (strcmp(method_str, "code-wifi") == 0)
                        {
                            if (node && cJSON_IsNumber(node) && payload && cJSON_IsString(payload))
                            {
                                controller::pairing_code_wifi(node_id, ssid_str, pwd_str, payload_str);
                            }
                        }
                        else if (strcmp(method_str, "code-wifi-thread") == 0)
                        {
                            if (node && cJSON_IsNumber(node) && payload && cJSON_IsString(payload))
                            {
                                const char *dataset = sys_settings.thread.TLVs;
                                size_t dataset_len = strlen(dataset) / 2;
                                uint8_t dataset_tlvs[dataset_len];
                                hex_string_to_bytes(dataset, dataset_tlvs, dataset_len);
                                controller::pairing_code_wifi_thread(node_id, ssid_str, pwd_str, payload_str, dataset_tlvs,
                                                                     dataset_len);
                            }
                        }
                    }
                }
            }
            else if (strcmp(action_str, "subs-attr") == 0)
            {
                ESP_LOGW(TAG, "Subscribe command");

                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) && cluster && cJSON_IsNumber(cluster) && attr && cJSON_IsNumber(attr) && min_interval && cJSON_IsNumber(min_interval) && max_interval && cJSON_IsNumber(max_interval))
                {
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
            else if (strcmp(action_str, "read-attr") == 0)
            {
                ESP_LOGW(TAG, "Read attribute command");
                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) && cluster && cJSON_IsNumber(cluster) && attr && cJSON_IsNumber(attr))
                {
                    read_command *cmdread = chip::Platform::New<read_command>(node_id, endpoint_id, cluster_id, attr_id, READ_ATTRIBUTE, OnAttributeData, nullptr, nullptr);
                    if (!cmdread)
                    {
                        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
                    }
                    else
                    {
                        chip::DeviceLayer::PlatformMgr().LockChipStack();
                        cmdread->send_command();
                        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid parameters for read-attr command");
                }
            }
            else if (strcmp(action_str, "write-attr") == 0)
            {
                ESP_LOGW(TAG, "Write attribute command");
                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) && cluster && cJSON_IsNumber(cluster) && attr && cJSON_IsNumber(attr) && attribute_val && cJSON_IsString(attribute_val))
                {
                    ESP_LOGW(TAG, "Writing attribute on node ID: %llu, endpoint ID: %u, cluster ID: %u, attr ID: %u", node_id, endpoint_id, cluster_id, attr_id);
                    controller::send_write_attr_command(node_id, endpoint_id, cluster_id, attr_id, attribute_val_str);
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid parameters for write-attr command");
                }
            }
            else if (strcmp(action_str, "invoke-cmd") == 0)
            {
                ESP_LOGW(TAG, "Invoke command");
                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) && cluster && cJSON_IsNumber(cluster) && val && cJSON_IsNumber(val))
                {
                    ESP_LOGW(TAG, "Invoking command on node ID: %llu, endpoint ID: %u, cluster ID: %u, command ID: %u", node_id, endpoint_id, cluster_id, command_id);
                    controller::send_invoke_cluster_command(node_id, endpoint_id, cluster_id, command_id, NULL);
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid parameters for invoke-cmd command");
                }
            }

            /*
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
            else if (strcmp(action_str, "read-attr") == 0)
            {
                ESP_LOGW(TAG, "Read attribute command");
                cJSON *node = cJSON_GetObjectItem(json, "node");
                cJSON *endpoint = cJSON_GetObjectItem(json, "endpoint");
                cJSON *cluster = cJSON_GetObjectItem(json, "cluster");
                cJSON *attr = cJSON_GetObjectItem(json, "attr");
                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) &&
                    cluster && cJSON_IsNumber(cluster) && attr && cJSON_IsNumber(attr))
                {
                    uint64_t node_id = (uint64_t)cJSON_GetNumberValue(node);
                    uint16_t endpoint_id = (uint16_t)cJSON_GetNumberValue(endpoint);
                    uint32_t cluster_id = (uint32_t)cJSON_GetNumberValue(cluster);
                    uint32_t attr_id = (uint32_t)cJSON_GetNumberValue(attr);

                    read_command *cmdread = chip::Platform::New<read_command>(node_id, endpoint_id, cluster_id, attr_id, READ_ATTRIBUTE, OnAttributeData, nullptr, nullptr);

                    if (!cmdread)
                    {
                        ESP_LOGE(TAG, "Failed to alloc memory for read_command");
                    }
                    else
                    {
                        chip::DeviceLayer::PlatformMgr().LockChipStack();
                        cmdread->send_command();
                        chip::DeviceLayer::PlatformMgr().UnlockChipStack();
                    }

                    ESP_LOGW(TAG, "Reading attribute on node ID: %llu, endpoint ID: %u, cluster ID: %u, attr ID: %u", node_id, endpoint_id, cluster_id, attr_id);
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid parameters for read-attr command");
                }
            }
            else if (strcmp(action_str, "write-attr") == 0)
            {
                ESP_LOGW(TAG, "Write attribute command");
                cJSON *node = cJSON_GetObjectItem(json, "node");
                cJSON *endpoint = cJSON_GetObjectItem(json, "endpoint");
                cJSON *cluster = cJSON_GetObjectItem(json, "cluster");
                cJSON *attr = cJSON_GetObjectItem(json, "attr");
                cJSON *value = cJSON_GetObjectItem(json, "value");
                if (node && cJSON_IsNumber(node) && endpoint && cJSON_IsNumber(endpoint) &&
                    cluster && cJSON_IsNumber(cluster) && attr && cJSON_IsNumber(attr) &&
                    value)
                {
                    uint64_t node_id = (uint64_t)cJSON_GetNumberValue(node);
                    uint16_t endpoint_id = (uint16_t)cJSON_GetNumberValue(endpoint);
                    uint32_t cluster_id = (uint32_t)cJSON_GetNumberValue(cluster);
                    uint32_t attr_id = (uint32_t)cJSON_GetNumberValue(attr);
                    char *attribute_val_str = value->valuestring;

                    controller::send_write_attr_command(node_id, endpoint_id, cluster_id, attr_id, attribute_val_str);
                    ESP_LOGW(TAG, "Writing attribute on node ID: %llu, endpoint ID: %u, cluster ID: %u, attr ID: %u", node_id, endpoint_id, cluster_id, attr_id);
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid parameters for write-attr command");
                }
            }
            */
            else
            {
                ESP_LOGE(TAG, "No valid 'actions' field in JSON");
            }

            cJSON_Delete(json);
        }
    }
}