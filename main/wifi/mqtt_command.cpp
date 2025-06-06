#include "settings.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "mqtt.h"

#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_controller_pairing_command.h>
// #include <esp_matter_controller_subscribe_command.h>
// #include <esp_matter_controller_write_command.h>
// #include <esp_matter_controller_read_command.h>
// #include <esp_matter_controller_cluster_command.h>

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
            snprintf(error_msg, sizeof(error_msg), "{\"%s\":\"INVALID_ARG\"}", action_type);
            mqtt_publish_data(eventTopic, error_msg);
            return;
        }

        // Копируем строку для безопасной работы с strtok
        char *input_copy = strdup(input_str);
        if (input_copy == nullptr)
        {
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "{\"%s\":\"ERR_NO_MEM\"}", action_type);
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
            chip::DeviceLayer::PlatformMgr().LockChipStack();
            result = esp_matter::command::controller_pairing(argc, argv);
            chip::DeviceLayer::PlatformMgr().UnlockChipStack();
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
        const char *result_str = (result == ESP_OK) ? "success" : esp_err_to_name(result);
        size_t jsonLen = strlen("{\"\":\"\"}") + strlen(action_type) + strlen(result_str) + 1;
        char *jsonPayload = (char *)malloc(jsonLen);
        if (jsonPayload == nullptr)
        {
            free(input_copy);
            ESP_LOGE(TAG, "Memory allocation for JSON payload failed");
            char error_msg[64];
            snprintf(error_msg, sizeof(error_msg), "{\"%s\":\"ERR_NO_MEM\"}", action_type);
            mqtt_publish_data(eventTopic, error_msg);
            return;
        }

        snprintf(jsonPayload, jsonLen, "{\"%s\":\"%s\"}", action_type, result_str);

        // Publish result
        esp_err_t mqtt_ret = mqtt_publish_data(eventTopic, jsonPayload);
        if (mqtt_ret != ESP_OK)
        {
            ESP_LOGE(TAG, "MQTT publish failed: %s", esp_err_to_name(mqtt_ret));
        }

        // Cleanup
        free(jsonPayload);
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
        mqtt_publish_data(eventTopic, "{\"TLVs\":\"NoInstance\"}");
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
            mqtt_publish_data(eventTopic, "{\"TLVs\":\"TooLong\"}");
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
            mqtt_publish_data(eventTopic, "{\"TLVs\":\"MemErr\"}");
            return;
        }

        snprintf(jsonPayload, jsonLen, "{\"TLVs\":\"%s\"}", sys_settings.thread.TLVs);

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
        mqtt_publish_data(eventTopic, "{\"TLVs\":\"Err\"}");
    }
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
        if (actions && cJSON_IsString(actions))
        {
            const char *action_str = actions->valuestring;
            if (strcmp(action_str, "reboot") == 0)
            {
                ESP_LOGW(TAG, "Reboot ESP");
                mqtt_publish_data(eventTopic, "{\"reboot\":\"Ok\"}");
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                esp_restart();
            }
            else if (strcmp(action_str, "factoryreset") == 0)
            {
                ESP_LOGW(TAG, "Matter factory reset");
                mqtt_publish_data(eventTopic, "{\"factoryreset\":\"Ok\"}");
                vTaskDelay(2000 / portTICK_PERIOD_MS);
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
                    mqtt_publish_data(eventTopic, "{\"dataset_init_new\":\"Err\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"dataset_init_new\":\"Ok\"}");
                }
                // vTaskDelay(2000 / portTICK_PERIOD_MS);
                payload_str = "dataset commit active";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"dataset_commit_active\":\"Err\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"dataset_commit_active\":\"Ok\"}");
                }
                // vTaskDelay(2000 / portTICK_PERIOD_MS);
                payload_str = "dataset active -x";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"dataset_active_x\":\"Err\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"dataset_active_x\":\"Ok\"}");
                }
                // vTaskDelay(1000 / portTICK_PERIOD_MS);
                payload_str = "ifconfig up";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"ifconfig_up\":\"Err\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"ifconfig_up\":\"Ok\"}");
                }
                // vTaskDelay(2000 / portTICK_PERIOD_MS);
                payload_str = "thread start";
                result = otcli_string_handler(payload_str);
                if (result != ESP_OK)
                {
                    printf("Error sending the command: %s\n", payload_str);
                    mqtt_publish_data(eventTopic, "{\"thread_start\":\"Err\"}");
                }
                else
                {
                    mqtt_publish_data(eventTopic, "{\"thread_start\":\"Ok\"}");
                }
                getTLVs(eventTopic);
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
            else if (strcmp(action_str, "test") == 0)
            {
                        }
            else
            {
                ESP_LOGE(TAG, "No valid 'actions' field in JSON");
            }

            cJSON_Delete(json);
        }
    }
}