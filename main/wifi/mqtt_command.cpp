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

#include "matter_callbacks.h"

#define CLI_INPUT_BUFF_LENGTH 256u

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
size_t get_array_size(const char *str)
{
    if (!str)
    {
        return 0;
    }
    size_t ret = 1;
    for (size_t i = 0; i < strlen(str); ++i)
    {
        if (str[i] == ',')
        {
            ret++;
        }
    }
    return ret;
}
esp_err_t string_to_uint32_array(const char *str, ScopedMemoryBufferWithSize<uint32_t> &uint32_array)
{
    size_t array_len = get_array_size(str);
    if (array_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_array.Calloc(array_len);
    if (!uint32_array.Get())
    {
        return ESP_ERR_NO_MEM;
    }
    char number[11]; // max(strlen("0xFFFFFFFF"), strlen("4294967295")) + 1
    const char *next_number_start = str;
    char *next_number_end = NULL;
    size_t next_number_len = 0;
    for (size_t i = 0; i < array_len; ++i)
    {
        next_number_end = strchr(next_number_start, ',');
        if (next_number_end > next_number_start)
        {
            next_number_len = std::min((size_t)(next_number_end - next_number_start), sizeof(number) - 1);
        }
        else if (i == array_len - 1)
        {
            next_number_len = strnlen(next_number_start, sizeof(number) - 1);
        }
        else
        {
            return ESP_ERR_INVALID_ARG;
        }
        strncpy(number, next_number_start, next_number_len);
        number[next_number_len] = 0;
        uint32_array[i] = string_to_uint32(number);
        if (next_number_end > next_number_start)
        {
            next_number_start = next_number_end + 1;
        }
    }
    return ESP_OK;
}

int char_to_int(char ch)
{
    if ('A' <= ch && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    else if ('a' <= ch && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    else if ('0' <= ch && ch <= '9')
    {
        return ch - '0';
    }
    return -1;
}

esp_err_t string_to_uint16_array(const char *str, ScopedMemoryBufferWithSize<uint16_t> &uint16_array)
{
    size_t array_len = get_array_size(str);
    if (array_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_array.Calloc(array_len);
    if (!uint16_array.Get())
    {
        return ESP_ERR_NO_MEM;
    }
    char number[7]; // max(strlen(0xFFFF), strlen(65535)) + 1
    const char *next_number_start = str;
    char *next_number_end = NULL;
    size_t next_number_len = 0;
    for (size_t i = 0; i < array_len; ++i)
    {
        next_number_end = strchr(next_number_start, ',');
        if (next_number_end > next_number_start)
        {
            next_number_len = std::min((size_t)(next_number_end - next_number_start), sizeof(number) - 1);
        }
        else if (i == array_len - 1)
        {
            next_number_len = strnlen(next_number_start, sizeof(number) - 1);
        }
        else
        {
            return ESP_ERR_INVALID_ARG;
        }
        strncpy(number, next_number_start, next_number_len);
        number[next_number_len] = 0;
        uint16_array[i] = string_to_uint16(number);
        if (next_number_end > next_number_start)
        {
            next_number_start = next_number_end + 1;
        }
    }
    return ESP_OK;
}

bool convert_hex_str_to_bytes(const char *hex_str, uint8_t *bytes, uint8_t &bytes_len)
{
    if (!hex_str)
    {
        return false;
    }
    size_t hex_str_len = strlen(hex_str);
    if (hex_str_len == 0 || hex_str_len % 2 != 0 || hex_str_len / 2 > bytes_len)
    {
        return false;
    }
    bytes_len = hex_str_len / 2;
    for (size_t i = 0; i < bytes_len; ++i)
    {
        int byte_h = char_to_int(hex_str[2 * i]);
        int byte_l = char_to_int(hex_str[2 * i + 1]);
        if (byte_h < 0 || byte_l < 0)
        {
            return false;
        }
        bytes[i] = (byte_h << 4) + byte_l;
    }
    return true;
}

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
esp_err_t controller_pairing(int argc, char **argv)
{
    VerifyOrReturnError(argc >= 3 && argc <= 6, ESP_ERR_INVALID_ARG);
    esp_err_t result = ESP_ERR_INVALID_ARG;

    if (strncmp(argv[0], "onnetwork", sizeof("onnetwork")) == 0)
    {
        VerifyOrReturnError(argc == 3, ESP_ERR_INVALID_ARG);

        uint64_t nodeId = string_to_uint64(argv[1]);
        uint32_t pincode = string_to_uint32(argv[2]);
        return controller::pairing_on_network(nodeId, pincode);

#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    }
    else if (strncmp(argv[0], "ble-wifi", sizeof("ble-wifi")) == 0)
    {
        VerifyOrReturnError(argc == 6, ESP_ERR_INVALID_ARG);

        uint64_t nodeId = string_to_uint64(argv[1]);
        uint32_t pincode = string_to_uint32(argv[4]);
        uint16_t disc = string_to_uint16(argv[5]);

        result = controller::pairing_ble_wifi(nodeId, pincode, disc, argv[2], argv[3]);
    }
    else if (strncmp(argv[0], "ble-thread", sizeof("ble-thread")) == 0)
    {
        VerifyOrReturnError(argc == 5, ESP_ERR_INVALID_ARG);

        uint8_t dataset_tlvs_buf[254];
        uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);
        if (!convert_hex_str_to_bytes(argv[2], dataset_tlvs_buf, dataset_tlvs_len))
        {
            return ESP_ERR_INVALID_ARG;
        }
        uint64_t node_id = string_to_uint64(argv[1]);
        uint32_t pincode = string_to_uint32(argv[3]);
        uint16_t disc = string_to_uint16(argv[4]);

        result = controller::pairing_ble_thread(node_id, pincode, disc, dataset_tlvs_buf, dataset_tlvs_len);
#else  // if !CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    }
    else if (strncmp(argv[0], "ble-wifi", sizeof("ble-wifi")) == 0 ||
             strncmp(argv[0], "ble-thread", sizeof("ble-thread")) == 0)
    {
        ESP_LOGE(TAG, "Please enable ENABLE_ESP32_BLE_CONTROLLER to use pairing %s command", argv[0]);
        return ESP_ERR_NOT_SUPPORTED;
#endif // CONFIG_ENABLE_ESP32_BLE_CONTROLLER
    }
    else if (strncmp(argv[0], "code", sizeof("code")) == 0)
    {
        VerifyOrReturnError(argc == 3, ESP_ERR_INVALID_ARG);

        uint64_t nodeId = string_to_uint64(argv[1]);
        const char *payload = argv[2];

        result = controller::pairing_code(nodeId, payload);
    }
    else if (strncmp(argv[0], "code-thread", sizeof("code-thread")) == 0)
    {
        VerifyOrReturnError(argc == 4, ESP_ERR_INVALID_ARG);

        uint64_t nodeId = string_to_uint64(argv[1]);
        const char *payload = argv[3];

        uint8_t dataset_tlvs_buf[254];
        uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);
        if (!convert_hex_str_to_bytes(argv[2], dataset_tlvs_buf, dataset_tlvs_len))
        {
            return ESP_ERR_INVALID_ARG;
        }

        result = controller::pairing_code_thread(nodeId, payload, dataset_tlvs_buf, dataset_tlvs_len);
    }
    else if (strncmp(argv[0], "code-wifi", sizeof("code-wifi")) == 0)
    {
        VerifyOrReturnError(argc == 5, ESP_ERR_INVALID_ARG);

        uint64_t nodeId = string_to_uint64(argv[1]);
        const char *ssid = argv[2];
        const char *password = argv[3];
        const char *payload = argv[4];

        result = controller::pairing_code_wifi(nodeId, ssid, password, payload);
    }
    else if (strncmp(argv[0], "code-wifi-thread", sizeof("code-wifi-thread")) == 0)
    {
        VerifyOrReturnError(argc == 6, ESP_ERR_INVALID_ARG);

        uint64_t nodeId = string_to_uint64(argv[1]);
        const char *ssid = argv[2];
        const char *password = argv[3];
        const char *payload = argv[4];

        uint8_t dataset_tlvs_buf[254];
        uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);
        if (!convert_hex_str_to_bytes(argv[5], dataset_tlvs_buf, dataset_tlvs_len))
        {
            return ESP_ERR_INVALID_ARG;
        }

        result = controller::pairing_code_wifi_thread(nodeId, ssid, password, payload, dataset_tlvs_buf,
                                                      dataset_tlvs_len);
    }

    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Pairing over code failed");
    }
    return result;
}
#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE

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

    const char *mqttPrefix = sys_settings.mqtt.prefix;
    const char *envtopic = "/event/matter/";
    char eventTopic[strlen(mqttPrefix) + strlen(envtopic) + 1];
    strcpy(eventTopic, mqttPrefix);
    strcat(eventTopic, topic);

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
                vTaskDelay(1000 / portTICK_PERIOD_MS);
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
                vTaskDelay(2000 / portTICK_PERIOD_MS);
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
                vTaskDelay(2000 / portTICK_PERIOD_MS);
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
                vTaskDelay(1000 / portTICK_PERIOD_MS);
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
                vTaskDelay(2000 / portTICK_PERIOD_MS);
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
            }
            else if (strcmp(action_str, "getTLVs") == 0)
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
            else if (strcmp(action_str, "setTLV") == 0)
            {
                // TODO
            }
            else if (strcmp(action_str, "pairing") == 0)
            {
                // Отпрака команд сопряжения нового устройства
                ESP_LOGW(TAG, "Pairing command");
                cJSON *payload = cJSON_GetObjectItem(json, "payload");
                if (payload && cJSON_IsString(payload))
                {
                    const char *input_str = payload->valuestring;
                    // Проверка входной строки
                    if (input_str == nullptr || strlen(input_str) == 0)
                    {
                        mqtt_publish_data(eventTopic, "{\"Pairing\":\"INVALID_ARG\"}");
                    }

                    // Копируем строку для безопасной работы с strtok
                    char *input_copy = strdup(input_str);
                    if (input_copy == nullptr)
                    {
                        mqtt_publish_data(eventTopic, "{\"Pairing\":\"ERR_NO_MEM\"}");
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

                    // Вызываем оригинальный обработчик
                    esp_err_t result = controller_pairing(argc, argv);

                    // Prepare MQTT payload
                    const char *result_str = (result == ESP_OK) ? "success" : esp_err_to_name(result);
                    size_t jsonLen = strlen("{\"pairing\":\"\"}") + strlen(result_str) + 1;
                    char *jsonPayload = (char *)malloc(jsonLen);
                    if (jsonPayload == nullptr)
                    {
                        free(input_copy);
                        ESP_LOGE(TAG, "Memory allocation for JSON payload failed");
                        mqtt_publish_data(eventTopic, "{\"Pairing\":\"ERR_NO_MEM\"}");
                    }

                    snprintf(jsonPayload, jsonLen, "{\"pairing\":\"%s\"}", result_str);

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
            else if (strcmp(action_str, "subs-attr") == 0)
            {
                /*
                                ESP_LOGW(TAG, "Subscribe command");

                                cJSON *payload = cJSON_GetObjectItem(json, "payload");
                                if (payload && cJSON_IsString(payload))
                                {
                                    const char *input_str = payload->valuestring;
                                    // Проверка входной строки
                                    if (input_str == nullptr || strlen(input_str) == 0)
                                    {
                                        mqtt_publish_data(eventTopic, "{\"Pairing\":\"INVALID_ARG\"}");
                                    }

                                    // Копируем строку для безопасной работы с strtok
                                    char *input_copy = strdup(input_str);
                                    if (input_copy == nullptr)
                                    {
                                        mqtt_publish_data(eventTopic, "{\"Pairing\":\"ERR_NO_MEM\"}");
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
                                    if (argc != 4)
                                    {
                                        mqtt_publish_data(eventTopic, "{\"subs-attr\":\"INVALID_ARG\"}");
                                        ESP_LOGE(TAG, "Invalid parameters for subs-attr command");
                                    }

                                    uint64_t node_id = string_to_uint64(argv[0]);
                                    ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
                                    ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
                                    ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
                                    ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
                                    ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
                                    ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], attribute_ids), TAG, "Failed to parse attribute IDs");
                                    uint16_t min_interval = string_to_uint16(argv[4]);
                                    uint16_t max_interval = string_to_uint16(argv[5]);
                                    ESP_LOGW(TAG, "Subscribing to node ID: %llu, endpoint ID: %u, cluster ID: %u, attr ID: %u", node_id, endpoint_ids, cluster_ids, attribute_ids);
                                    subscribe_command *cmd = chip::Platform::New<subscribe_command>(node_id, endpoint_ids, cluster_ids, attribute_ids, SUBSCRIBE_ATTRIBUTE, min_interval, max_interval, true, OnAttributeData, nullptr, nullptr, nullptr);
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
                                    */
            }
            else
            {
                ESP_LOGE(TAG, "No valid 'actions' field in JSON");
            }

            cJSON_Delete(json);
        }
    }
}