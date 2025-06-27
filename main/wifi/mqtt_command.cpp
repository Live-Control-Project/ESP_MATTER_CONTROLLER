#include "settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt.h"

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_controller_pairing_command.h>
// #include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_write_command.h>
// #include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_cluster_command.h>

#include <esp_matter_controller_utils.h>
#include <esp_matter_client.h>
#include <esp_matter_controller_client.h>

#include <esp_check.h>

#include <freertos/FreeRTOS.h>
#include <lib/shell/Engine.h>
#include <memory>
#include <platform/ESP32/OpenthreadLauncher.h>

#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <esp_openthread.h>
#include <esp_err.h>

// #include "../matter/matter_command.h"
#include "matter_command.h"
#include "matter_callbacks.h"

#include "devices.h"

#include <stdio.h>
#include "cJSON.h"

#include "platform/PlatformManager.h" // Add if not present

extern matter_controller_t g_controller;
#define CLI_INPUT_BUFF_LENGTH 256u

using namespace esp_matter;
using namespace esp_matter::controller;
using namespace chip;
using namespace chip::app::Clusters;

static const char *TAG = "MQTT";

// Отправка комманд создания новой Thread сети
static esp_err_t otcli_string_handler(const char *input_str)
{
    if (input_str == nullptr || strlen(input_str) >= CLI_INPUT_BUFF_LENGTH)
    {
        return ESP_FAIL;
    }

    std::unique_ptr<char[]> cli_str(new char[CLI_INPUT_BUFF_LENGTH]);
    memset(cli_str.get(), 0, CLI_INPUT_BUFF_LENGTH);
    strncpy(cli_str.get(), input_str, CLI_INPUT_BUFF_LENGTH - 1);

    if (cli_transmit_task_post(std::move(cli_str)) != CHIP_NO_ERROR)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

extern "C" void handle_command(cJSON *json, const char *action_type, const char *eventTopic)
{

    ESP_LOGW(TAG, "%s command", action_type);
    cJSON *payload = cJSON_GetObjectItem(json, "payload");
    if (payload && cJSON_IsString(payload))
    {
        const char *input_str = payload->valuestring;
        // Проверка входной строки
        if (input_str == nullptr || strlen(input_str) == 0)
        {
            char error_msg[64];
            // формат {"action":action_type,"status":"INVALID_ARG"}
            snprintf(error_msg, sizeof(error_msg), "{\"action\":\"%s\",\"status\":\"INVALID_ARG\"}", action_type);
            mqtt_publish_data(eventTopic, error_msg);
            return;
        }

        // Копируем строку для безопасной работы с strtok
        char *input_copy = strdup(input_str);
        if (input_copy == nullptr)
        {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "{\"action\":\"%s\",\"status\":\"ERR_NO_MEM\"}", action_type);
            mqtt_publish_data(eventTopic, error_msg);
            return;
        }

        // Разбиваем строку на токены
        char *argv[10];
        int argc = 0;

        char *token = strtok(input_copy, " ");
        while (token != nullptr && argc < 10)
        {
            argv[argc++] = token;
            token = strtok(nullptr, " ");
        }
        esp_err_t result = ESP_FAIL;
        // Вызываем  обработчик
        if (strcmp(action_type, "pairing") == 0)
        {

            // chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_pairing(argc, argv);
            // chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "subs-attr") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_subscribe_attr(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "invoke-cmd") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_invoke_command(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "read-attr") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_read_attr(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "write-attr") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_write_attr(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "read-event") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_read_event(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "subscribe-event") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_subscribe_event(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "shutdown-subscription") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_shutdown_subscription(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "shutdown-subscriptions") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_shutdown_subscriptions(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        if (strcmp(action_type, "shutdown-all-subscriptions") == 0)
        {
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_shutdown_all_subscriptions(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
        }
        // Prepare MQTT payload

        if (result != ESP_OK)
        {
            free(input_copy);
            ESP_LOGE(TAG, "Memory allocation for JSON payload failed");
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "{\"action\":\"%s\",\"status\":\"ERR_NO_MEM\"}", action_type);
            mqtt_publish_data(eventTopic, error_msg);
            return;
        }
        else
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "{\"action\":\"%s\",\"status\":\"progress\"}", action_type);
            // Publish result
            esp_err_t mqtt_ret = mqtt_publish_data(eventTopic, msg);
            if (mqtt_ret != ESP_OK)
            {
                ESP_LOGE(TAG, "MQTT publish failed: %s", esp_err_to_name(mqtt_ret));
            }
        }
        // Cleanup
        free(input_copy);
    }
}
void getTLVs(const char *eventTopic)
{
    ESP_LOGW(TAG, "Get TLVs");

    // Получаем экземпляр OpenThread
    otInstance *instance = (otInstance *)esp_openthread_get_instance();
    if (instance == nullptr)
    {
        ESP_LOGE(TAG, "OpenThread instance is not initialized");
        // формат {"action":action_type,"status":"INVALID_ARG"}
        mqtt_publish_data(eventTopic, "{\"action\":\"getTLVs\",\"status\":\"NoInstance\"}");
        return;
    }

    // Получаем активный набор данных Thread
    otOperationalDatasetTlvs datasetTlvs;
    otError error = otDatasetGetActiveTlvs(instance, &datasetTlvs);

    if (error == OT_ERROR_NONE)
    {
        // Преобразуем TLVs в hex-строку
        char tlvsStr[OT_OPERATIONAL_DATASET_MAX_LENGTH * 2 + 1];
        for (size_t i = 0; i < datasetTlvs.mLength; i++)
        {
            snprintf(&tlvsStr[i * 2], 3, "%02x", datasetTlvs.mTlvs[i]);
        }
        tlvsStr[datasetTlvs.mLength * 2] = '\0';

        // Проверяем, поместится ли строка в массив
        if (strlen(tlvsStr) >= sizeof(sys_settings.thread.TLVs))
        {
            ESP_LOGE(TAG, "TLVs string too long for buffer");
            mqtt_publish_data(eventTopic, "{\"action\":\"getTLVs\",\"status\":\"TooLong\"}");
            return;
        }

        // Копируем строку в массив фиксированного размера
        strncpy(sys_settings.thread.TLVs, tlvsStr, sizeof(sys_settings.thread.TLVs));
        sys_settings.thread.TLVs[sizeof(sys_settings.thread.TLVs) - 1] = '\0'; // Гарантируем завершение строки

        // Формируем JSON
        size_t jsonLen = strlen("{\"TLVs\":\"\"}") + strlen(sys_settings.thread.TLVs) + 1;
        char *jsonPayload = (char *)malloc(jsonLen);
        if (!jsonPayload)
        {
            ESP_LOGE(TAG, "JSON payload allocation failed");
            mqtt_publish_data(eventTopic, "{\"action\":\"getTLVs\",\"status\":\"MemErr\"}");
            return;
        }

        // формат {"action":getTLVs,"status":"INVALID_ARG"}
        snprintf(jsonPayload, jsonLen, "{\"action\":\"getTLVs\",\"status\":\"%s\"}", sys_settings.thread.TLVs);

        // Публикуем
        esp_err_t ret = mqtt_publish_data(eventTopic, jsonPayload);
        free(jsonPayload);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "MQTT publish failed: %s", esp_err_to_name(ret));
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get TLVs: %d", error);
        mqtt_publish_data(eventTopic, "{\"action\":\"getTLVs\",\"status\":\"Err\"}");
    }
}

struct ClusterCommandArgs
{
    uint64_t node_id;
    uint64_t endpoint_id;
    uint64_t cluster_id;
    uint32_t attribute_id;
    char value[64]; // Use char array for string value
                    // uint32_t value;
};

// 2. Define a static function to call from ScheduleWork
static void SendInvokeClusterCommand(intptr_t arg)
{
    ClusterCommandArgs *args = reinterpret_cast<ClusterCommandArgs *>(arg);
    controller::send_invoke_cluster_command(
        args->node_id,
        static_cast<uint16_t>(args->endpoint_id),
        static_cast<uint32_t>(args->cluster_id),
        args->attribute_id,
        // args->value, // Pass as string
        NULL,
        chip::MakeOptional(1000));
    delete args; // Clean up
}
static void SendWriteClusterCommand(intptr_t arg)
{
    ClusterCommandArgs *args = reinterpret_cast<ClusterCommandArgs *>(arg);
    esp_matter::controller::send_write_attr_command(
        args->node_id,
        static_cast<uint16_t>(args->endpoint_id),
        static_cast<uint32_t>(args->cluster_id),
        args->attribute_id,
        args->value, // Pass as string
        chip::MakeOptional(1000));
    delete args; // Clean up
}

extern "C" void handle_mqtt_data(esp_mqtt_event_handle_t event)
{
    // Extract topic and data
    char topic[event->topic_len + 1];
    memcpy(topic, event->topic, event->topic_len);
    topic[event->topic_len] = '\0';

    char data[event->data_len + 1];
    memcpy(data, event->data, event->data_len);
    data[event->data_len] = '\0';

    const char *mqttPrefix = sys_settings.mqtt.prefix;
    const char *envtopic = "/event/matter/";
    char eventTopic[strlen(mqttPrefix) + strlen(envtopic) + 1];
    strcpy(eventTopic, mqttPrefix);
    strcat(eventTopic, envtopic);

    //    ESP_LOGI(TAG, "TOPIC=%s", topic);
    //    ESP_LOGI(TAG, "DATA=%s", data);

    if (strstr(topic, "/td/matter") != NULL)
    {
        ESP_LOGI(TAG, "TOPIC=%s", topic);
        ESP_LOGI(TAG, "DATA=%s", data);

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
        if (strstr(topic, "/td/matter_csa") != NULL)
        {
            // topic = "esp_matter_server/td/matter_csa/Node_id/Endpoint_id"
            // получаем из топика Node_id и Endpoint_id
            ESP_LOGI(TAG, "Processing /td/matter_csa topic");
            char *node_id_str = strstr(topic, "/td/matter_csa/") + strlen("/td/matter_csa/");
            char *endpoint_id_str = strchr(node_id_str, '/');
            if (endpoint_id_str != NULL)
            {
                *endpoint_id_str = '\0';
                endpoint_id_str++;
            }
            else
            {
                ESP_LOGE(TAG, "Invalid topic format for /td/matter_csa");
                cJSON_Delete(json);
                return;
            }
            uint64_t node_id = strtoull(node_id_str, NULL, 0);
            uint64_t endpoint_id = strtoull(endpoint_id_str, NULL, 0);
            ESP_LOGI(TAG, "Node ID: 0x%" PRIx64 ", Endpoint ID: 0x%" PRIx64, node_id, endpoint_id);

            // разбираем JSON
            cJSON *root = cJSON_Parse(data);
            if (root == NULL)
            {
                ESP_LOGE(TAG, "Ошибка парсинга JSON!");
                return;
            }

            cJSON *outer_item = NULL;
            cJSON_ArrayForEach(outer_item, root)
            {
                const char *cluster = outer_item->string;
                uint64_t cluster_id = strtoull(cluster, NULL, 0);
                ESP_LOGI(TAG, "cluster_id: %s\n", cluster);

                // Перебираем внутренние ключи
                cJSON *inner_item = NULL;
                cJSON_ArrayForEach(inner_item, outer_item)
                {
                    const char *attribute = inner_item->string;
                    uint64_t attribute_id = strtoull(attribute, NULL, 0);

                    ESP_LOGI(TAG, "  attribute_id: %s\n", attribute);
                    if (cJSON_IsNumber(inner_item))
                    {
                        ESP_LOGI(TAG, "Значение int: %d\n", inner_item->valueint);
                    }
                    else if (cJSON_IsString(inner_item))
                    {
                        ESP_LOGI(TAG, "Значение string: %s\n", inner_item->valuestring);
                    }
                    else if (cJSON_IsBool(inner_item))
                    {
                        ESP_LOGI(TAG, "Значение bool: %s\n", inner_item->valueint ? "true" : "false");
                    }
                    if (cluster_id == 6)
                    {
                        auto *args = new ClusterCommandArgs{
                            node_id,
                            endpoint_id,
                            cluster_id,
                            static_cast<uint32_t>(attribute_id)};
                        strncpy(args->value, inner_item->valuestring ? inner_item->valuestring : "", sizeof(args->value) - 1);
                        args->value[sizeof(args->value) - 1] = '\0';

                        chip::DeviceLayer::PlatformMgr().ScheduleWork(SendInvokeClusterCommand, reinterpret_cast<intptr_t>(args));
                    }
                    else
                    {
                        auto *args = new ClusterCommandArgs{
                            node_id,
                            endpoint_id,
                            cluster_id,
                            static_cast<uint32_t>(attribute_id)};
                        strncpy(args->value, inner_item->valuestring ? inner_item->valuestring : "", sizeof(args->value) - 1);
                        args->value[sizeof(args->value) - 1] = '\0';

                        chip::DeviceLayer::PlatformMgr().ScheduleWork(SendWriteClusterCommand, reinterpret_cast<intptr_t>(args));
                    }
                }
            }

            cJSON_Delete(root);
        }
    }

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

        cJSON *action = cJSON_GetObjectItem(json, "action");
        if (action && cJSON_IsString(action))
        {
            const char *action_str = action->valuestring;
            if (strcmp(action_str, "reboot") == 0)
            {
                ESP_LOGW(TAG, "Reboot ESP");

                mqtt_publish_data(eventTopic, "{\"action\":\"reboot\",\"status\":\"progress\"}");

                vTaskDelay(3000 / portTICK_PERIOD_MS);
                esp_restart();
            }
            else if (strcmp(action_str, "factoryreset") == 0)
            {
                ESP_LOGW(TAG, "Matter factory reset");
                mqtt_publish_data(eventTopic, "{\"action\":\"factoryreset\",\"status\":\"progress\"}");
                settings_set_defaults();
                matter_controller_free(&g_controller);
                // сохраняем nvs
                esp_err_t ret = save_devices_to_nvs(&g_controller);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to delete devices from NVS: 0x%x", ret);
                }

                else
                {
                    ESP_LOGI(TAG, "Devices deleted from NVS");
                }
                vTaskDelay(5000 / portTICK_PERIOD_MS);
                esp_matter::factory_reset();
            }
            else if (strcmp(action_str, "initOpenThread") == 0)
            {

                ESP_LOGW(TAG, "Init new OpenThread network");
                // vTaskDelay(1000 / portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "dataset init new");
                const char *payload_str = "dataset init new";
                esp_err_t result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"action\":\"dataset_init_new\",\"status\":\"failed\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"action\":\"dataset_init_new\",\"status\":\"success\"}");
                }
                // vTaskDelay(2000 / portTICK_PERIOD_MS);
                payload_str = "dataset commit active";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"action\":\"dataset_commit_active\",\"status\":\"failed\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"action\":\"dataset_commit_active\",\"status\":\"success\"}");
                }
                // vTaskDelay(2000 / portTICK_PERIOD_MS);
                payload_str = "dataset active -x";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"action\":\"dataset_active_x\",\"status\":\"failed\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"action\":\"dataset_active_x\",\"status\":\"success\"}");
                }
                // vTaskDelay(1000 / portTICK_PERIOD_MS);
                payload_str = "ifconfig up";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"action\":\"ifconfig_up\",\"status\":\"failed\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"action\":\"ifconfig_up\",\"status\":\"success\"}");
                }
                // vTaskDelay(2000 / portTICK_PERIOD_MS);
                payload_str = "thread start";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"action\":\"thread_start\",\"status\":\"failed\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"action\":\"thread_start\",\"status\":\"success\"}");
                }
                getTLVs(eventTopic);
                // сохраняем nvs system_settings_t sys_settings;
                esp_err_t ret = settings_save_to_nvs();
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to save system settings to NVS: 0x%x", ret);
                }
                else
                {
                    ESP_LOGI(TAG, "System settings saved to NVS");
                }
            }
            else if (strcmp(action_str, "getTLVs") == 0)
            {
                getTLVs(eventTopic);
            }
            else if (strcmp(action_str, "setTLV") == 0)
            {
                // TODO
            }
            else if (strcmp(action_str, "pairing") == 0)
            {
                handle_command(json, "pairing", eventTopic);
            }
            else if (strcmp(action_str, "subs-attr") == 0)
            {
                handle_command(json, "subs-attr", eventTopic);
            }
            else if (strcmp(action_str, "invoke-cmd") == 0)
            {
                handle_command(json, "invoke-cmd", eventTopic);
            }
            else if (strcmp(action_str, "read-attr") == 0)
            {
                handle_command(json, "read-attr", eventTopic);
            }
            else if (strcmp(action_str, "write-attr") == 0)
            {
                handle_command(json, "write-attr", eventTopic);
            }
            else if (strcmp(action_str, "read-event") == 0)
            {
                handle_command(json, "read-event", eventTopic);
            }
            else if (strcmp(action_str, "subscribe-event") == 0)
            {
                handle_command(json, "subscribe-event", eventTopic);
            }
            else if (strcmp(action_str, "shutdown-subscription") == 0)
            {
                handle_command(json, "shutdown-subscription", eventTopic);
            }
            else if (strcmp(action_str, "shutdown-subscriptions") == 0)
            {
                handle_command(json, "shutdown-subscriptions", eventTopic);
            }
            else if (strcmp(action_str, "shutdown-all-subscriptions") == 0)
            {
                handle_command(json, "shutdown-all-subscriptions", eventTopic);
            }
            else if (strcmp(action_str, "subs-all-attrs") == 0)
            {
                chip::DeviceLayer::PlatformMgr().ScheduleWork(
                    [](intptr_t ctx)
                    {
                        esp_err_t ret = subscribe_all_marked_attributes(&g_controller);
                        if (ret != ESP_OK)
                        {
                            ESP_LOGE(TAG, "Failed to subscribe to all marked attributes: %s", esp_err_to_name(ret));
                        }
                    },
                    0);
            }
            else if (strcmp(action_str, "log_controller_structure") == 0)
            {
                log_controller_structure(&g_controller);
            }
            else
            {
                ESP_LOGE(TAG, "No valid 'action' field in JSON");
            }

            cJSON_Delete(json);
        }
    }
}