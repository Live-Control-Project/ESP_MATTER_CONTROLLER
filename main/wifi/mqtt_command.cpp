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
#include <math.h>
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
        if (strcmp(action_type, "remove-node") == 0)

        {
            uint64_t node_id = strtoull(argv[0], NULL, 10); // Десятичное число
                                                            // или, если payload содержит HEX:
                                                            // uint64_t node_id = strtoull(payload, NULL, 16);
            result = remove_device(&g_controller, node_id);
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

    // Для OnOff cluster (6) и команд 0, 1, 2 не передавать value!
    const char *cmd_data = args->value;
    if (args->cluster_id == 6 && (args->attribute_id == 0 || args->attribute_id == 1 || args->attribute_id == 2))
    {
        cmd_data = nullptr;
    }

    controller::send_invoke_cluster_command(
        args->node_id,
        static_cast<uint16_t>(args->endpoint_id),
        static_cast<uint32_t>(args->cluster_id),
        args->attribute_id,
        cmd_data,
        chip::NullOptional);
    delete args; // Clean up
}
/*
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
*/
// Преобразование RGB в XY (упрощенный метод)
void rgb_to_xy(uint8_t r, uint8_t g, uint8_t b, uint16_t *x, uint16_t *y)
{
    float red = r / 255.0f;
    float green = g / 255.0f;
    float blue = b / 255.0f;

    // Гамма-коррекция
    red = (red > 0.04045f) ? powf((red + 0.055f) / 1.055f, 2.4f) : (red / 12.92f);
    green = (green > 0.04045f) ? powf((green + 0.055f) / 1.055f, 2.4f) : (green / 12.92f);
    blue = (blue > 0.04045f) ? powf((blue + 0.055f) / 1.055f, 2.4f) : (blue / 12.92f);

    // Преобразование в XYZ
    float X = red * 0.4124564f + green * 0.3575761f + blue * 0.1804375f;
    float Y = red * 0.2126729f + green * 0.7151522f + blue * 0.0721750f;
    float Z = red * 0.0193339f + green * 0.1191920f + blue * 0.9503041f;

    // Нормализация в xy
    float sum = X + Y + Z;
    float x_val = (sum == 0) ? 0.3127f : (X / sum); // fallback к D65
    float y_val = (sum == 0) ? 0.3290f : (Y / sum);

    // Масштабирование для Zigbee (0–65279)
    *x = (uint16_t)(x_val * 65279.0f);
    *y = (uint16_t)(y_val * 65279.0f);
}

static void SendWriteClusterCommand(intptr_t arg)
{
    ClusterCommandArgs *args = reinterpret_cast<ClusterCommandArgs *>(arg);

    chip::Platform::ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
    chip::Platform::ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
    chip::Platform::ScopedMemoryBufferWithSize<uint32_t> attribute_ids;

    endpoint_ids.Alloc(1);
    cluster_ids.Alloc(1);
    attribute_ids.Alloc(1);

    if (!endpoint_ids.Get() || !cluster_ids.Get() || !attribute_ids.Get())
    {
        ESP_LOGE(TAG, "Failed to allocate memory for attribute write");
        delete args;
        return;
    }

    endpoint_ids[0] = static_cast<uint16_t>(args->endpoint_id);
    cluster_ids[0] = static_cast<uint32_t>(args->cluster_id);
    attribute_ids[0] = args->attribute_id;

    // Формируем JSON-объект для значения атрибута: {"0:U8": <value>}
    char attr_val_json[80];
    snprintf(attr_val_json, sizeof(attr_val_json), "{\"0:U8\": %s}", args->value);

    esp_matter::controller::send_write_attr_command(
        args->node_id,
        endpoint_ids,
        cluster_ids,
        attribute_ids,
        attr_val_json,
        chip::MakeOptional(1000));
    delete args;
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
    if (strstr(topic, "/td/matter") != NULL)
    {
        ESP_LOGI(TAG, "TOPIC=%s", topic);
        ESP_LOGI(TAG, "DATA=%s", data);

        // получаем из топика Node_id и Endpoint_id
        char *node_id_str = strstr(topic, "/td/matter/") + strlen("/td/matter/");
        char *endpoint_id_str = strchr(node_id_str, '/');
        uint64_t endpoint_id = 1;
        if (endpoint_id_str != NULL)
        {
            *endpoint_id_str = '\0';
            endpoint_id_str++;
            endpoint_id = strtoull(endpoint_id_str, NULL, 0);
        }

        uint64_t node_id = strtoull(node_id_str, NULL, 0);
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
            // Проверяем, что текущий элемент - это объект с ключом "status"
            if (outer_item->string && strcmp(outer_item->string, "status") == 0)
            {
                // Получаем значение элемента (уже сам outer_item содержит значение)
                if (cJSON_IsString(outer_item))
                {
                    // Определяем команду на основе значения
                    ClusterCommandArgs *args = nullptr;

                    // Приводим значение к нижнему регистру для сравнения
                    char lower_val[32];
                    strncpy(lower_val, outer_item->valuestring, sizeof(lower_val) - 1);
                    lower_val[sizeof(lower_val) - 1] = '\0';
                    for (char *p = lower_val; *p; ++p)
                        *p = tolower(*p);

                    // Устанавливаем соответствующий аргумент
                    if (strcmp(lower_val, "on") == 0 || strcmp(lower_val, "1") == 0)
                    {
                        args = new ClusterCommandArgs{node_id, endpoint_id, 6, 1};
                    }
                    else if (strcmp(lower_val, "off") == 0 || strcmp(lower_val, "0") == 0)
                    {
                        args = new ClusterCommandArgs{node_id, endpoint_id, 6, 0};
                    }
                    else if (strcmp(lower_val, "toggle") == 0)
                    {
                        args = new ClusterCommandArgs{node_id, endpoint_id, 6, 2};
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Unknown status value: %s", outer_item->valuestring);
                        continue;
                    }

                    // Копируем значение
                    strncpy(args->value, outer_item->valuestring, sizeof(args->value) - 1);
                    args->value[sizeof(args->value) - 1] = '\0';

                    // Отправляем команду
                    chip::DeviceLayer::PlatformMgr().ScheduleWork(
                        SendInvokeClusterCommand,
                        reinterpret_cast<intptr_t>(args));
                }
            }

            if (outer_item->string && strcmp(outer_item->string, "level") == 0)
            {
                if (cJSON_IsNumber(outer_item))
                {
                    // 1. Включаем устройство (OnOff cluster, On command)
                    {
                        ClusterCommandArgs *on_args = new ClusterCommandArgs{
                            node_id,
                            endpoint_id,
                            6, // OnOff cluster
                            1  // On command
                        };
                        on_args->value[0] = '\0';
                        chip::DeviceLayer::PlatformMgr().ScheduleWork(
                            SendInvokeClusterCommand,
                            reinterpret_cast<intptr_t>(on_args));
                    }

                    // 2. Формируем JSON для MoveToLevel: {"0:U8": <level>, "1:U16": 0, "2:U8": 0, "3:U8": 0}
                    char cmd_data[80];
                    uint8_t level = (uint8_t)outer_item->valueint;
                    if (level > 254)
                        level = 254;
                    snprintf(cmd_data, sizeof(cmd_data),
                             "{\"0:U8\": %u, \"1:U16\": 0, \"2:U8\": 0, \"3:U8\": 0}", level);

                    ClusterCommandArgs *args = new ClusterCommandArgs{
                        node_id,
                        endpoint_id,
                        8, // LevelControl cluster
                        0  // MoveToLevel command
                    };
                    strncpy(args->value, cmd_data, sizeof(args->value) - 1);
                    args->value[sizeof(args->value) - 1] = '\0';

                    chip::DeviceLayer::PlatformMgr().ScheduleWork(
                        SendInvokeClusterCommand,
                        reinterpret_cast<intptr_t>(args));
                }
            }
            // цвет {"color":[167,255,120]}
            if (strcmp(outer_item->string, "color") == 0 && cJSON_IsArray(outer_item))
            {

                // 2. Получаем значения RGB из массива
                cJSON *r_item = cJSON_GetArrayItem(outer_item, 0);
                cJSON *g_item = cJSON_GetArrayItem(outer_item, 1);
                cJSON *b_item = cJSON_GetArrayItem(outer_item, 2);

                if (r_item && g_item && b_item &&
                    cJSON_IsNumber(r_item) && cJSON_IsNumber(g_item) && cJSON_IsNumber(b_item))
                {
                    uint8_t r = (uint8_t)r_item->valueint;
                    uint8_t g = (uint8_t)g_item->valueint;
                    uint8_t b = (uint8_t)b_item->valueint;
                    uint16_t x, y;
                    // 1. Установка Options=1
                    {
                        char options_cmd[20];
                        snprintf(options_cmd, sizeof(options_cmd), "{\"0:U8\": 1}");

                        ClusterCommandArgs *options_args = new ClusterCommandArgs{
                            node_id, endpoint_id, 768, 0x10};
                        strncpy(options_args->value, options_cmd, sizeof(options_args->value) - 1);
                        chip::DeviceLayer::PlatformMgr().ScheduleWork(
                            SendInvokeClusterCommand, reinterpret_cast<intptr_t>(options_args));

                        vTaskDelay(100 / portTICK_PERIOD_MS);
                    }

                    // 2. Установка ColorMode=1 (XY)
                    {
                        char mode_cmd[20];
                        snprintf(mode_cmd, sizeof(mode_cmd), "{\"0:U8\": 1}");

                        ClusterCommandArgs *mode_args = new ClusterCommandArgs{
                            node_id, endpoint_id, 768, 8};
                        strncpy(mode_args->value, mode_cmd, sizeof(mode_args->value) - 1);
                        chip::DeviceLayer::PlatformMgr().ScheduleWork(
                            SendInvokeClusterCommand, reinterpret_cast<intptr_t>(mode_args));

                        vTaskDelay(200 / portTICK_PERIOD_MS);
                    }

                    // 3. Отправка цвета
                    rgb_to_xy(r, g, b, &x, &y);

                    char color_cmd[50];
                    snprintf(color_cmd, sizeof(color_cmd),
                             "{\"0:U16\": %u, \"1:U16\": %u, \"2:U16\": 0}", x, y);

                    ClusterCommandArgs *color_args = new ClusterCommandArgs{
                        node_id, endpoint_id, 768, 7};
                    strncpy(color_args->value, color_cmd, sizeof(color_args->value) - 1);
                    chip::DeviceLayer::PlatformMgr().ScheduleWork(
                        SendInvokeClusterCommand, reinterpret_cast<intptr_t>(color_args));
                }
            }
        }
    }

    // Check for /td/matter_csa topic
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
            else if (strcmp(action_str, "remove-node") == 0)
            {
                handle_command(json, "remove-node", eventTopic);
            }
            else
            {
                ESP_LOGE(TAG, "No valid 'action' field in JSON");
            }

            cJSON_Delete(json);
        }
    }
}