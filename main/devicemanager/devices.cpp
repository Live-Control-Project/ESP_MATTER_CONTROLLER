#include "devices.h"
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include "settings.h"
#include <nvs_flash.h>
#include <nvs.h>
#include "EntryToText.h"
#include "matter_command.h"
#include "mqtt.h"
#include "matter_callbacks.h"
#include <esp_matter_controller_subscribe_command.h>
#include <set>
#include "app_priv.h"
#include "app_matter_ctrl.h"
#define NVS_NAMESPACE "matter_devices"
#define NVS_KEY "devices_list"

const char *TAG_device = "devices.cpp";
extern matter_controller_t g_controller;

// Инициализация девайсов
void matter_controller_init(matter_controller_t *controller, uint64_t controller_node_id, uint16_t fabric_id)
{
    if (controller == NULL)
    {
        ESP_LOGE(TAG_device, "Controller pointer is NULL!");
        return;
    }

    // Инициализация NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Инициализация полей
    memset(controller, 0, sizeof(matter_controller_t));
    controller->controller_node_id = controller_node_id;
    controller->fabric_id = fabric_id;

    // Загрузка сохраненных устройств
    esp_err_t load_err = load_devices_from_nvs(controller);
    if (load_err == ESP_OK)
    {
        ESP_LOGI(TAG_device, "Loaded %d devices from NVS", controller->nodes_count);
    }
    else
    {
        ESP_LOGW(TAG_device, "No saved devices found in NVS (err: 0x%x)", load_err);
    }
    //  log_controller_structure(&g_controller);
}

// Поиск узла по ID
matter_device_t *find_node(matter_controller_t *controller, uint64_t node_id)
{
    matter_device_t *current = controller->nodes_list;
    while (current != NULL)
    {
        if (current->node_id == node_id)
        {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

// Добавление нового узла
matter_device_t *add_node(matter_controller_t *controller, uint64_t node_id, const char *model_name, const char *vendor_name)
{
    matter_device_t *new_node = (matter_device_t *)malloc(sizeof(matter_device_t));
    if (!new_node)
        return NULL;

    memset(new_node, 0, sizeof(matter_device_t));
    new_node->node_id = node_id;
    new_node->is_online = true;
    strncpy(new_node->model_name, model_name, sizeof(new_node->model_name) - 1);
    strncpy(new_node->vendor_name, vendor_name, sizeof(new_node->vendor_name) - 1);
    new_node->next = controller->nodes_list;
    controller->nodes_list = new_node;
    controller->nodes_count++;
    return new_node;
}

// Добавление endpoint к узлу
endpoint_entry_t *add_endpoint(matter_device_t *node, uint16_t endpoint_id, const char *endpoint_name)
{
    endpoint_entry_t *new_endpoints = (endpoint_entry_t *)realloc(node->endpoints,
                                                                  (node->endpoints_count + 1) * sizeof(endpoint_entry_t));
    if (!new_endpoints)
        return NULL;

    node->endpoints = new_endpoints;
    endpoint_entry_t *ep = &node->endpoints[node->endpoints_count];
    memset(ep, 0, sizeof(endpoint_entry_t));
    ep->endpoint_id = endpoint_id;
    if (endpoint_name)
    {
        strncpy(ep->endpoint_name, endpoint_name, sizeof(ep->endpoint_name) - 1);
    }
    node->endpoints_count++;
    return ep;
}

// Добавление кластера к узлу
matter_cluster_t *add_cluster(matter_device_t *node, uint32_t cluster_id, const char *cluster_name, bool is_client)
{
    matter_cluster_t **clusters = is_client ? &node->client_clusters : &node->server_clusters;
    uint16_t *count = is_client ? &node->client_clusters_count : &node->server_clusters_count;

    matter_cluster_t *new_clusters = (matter_cluster_t *)realloc(*clusters,
                                                                 (*count + 1) * sizeof(matter_cluster_t));
    if (!new_clusters)
        return NULL;

    *clusters = new_clusters;
    matter_cluster_t *cl = &(*clusters)[*count];
    memset(cl, 0, sizeof(matter_cluster_t));
    cl->cluster_id = cluster_id;
    if (cluster_name)
    {
        strncpy(cl->cluster_name, cluster_name, sizeof(cl->cluster_name) - 1);
    }
    cl->is_client = is_client;
    (*count)++;
    return cl;
}

// Добавление атрибута к кластеру
matter_attribute_t *add_attribute(matter_cluster_t *cluster, uint32_t attribute_id, const char *attribute_name)
{
    matter_attribute_t *new_attributes = (matter_attribute_t *)realloc(cluster->attributes,
                                                                       (cluster->attributes_count + 1) * sizeof(matter_attribute_t));
    if (!new_attributes)
        return NULL;

    cluster->attributes = new_attributes;
    matter_attribute_t *attr = &cluster->attributes[cluster->attributes_count];
    memset(attr, 0, sizeof(matter_attribute_t));
    attr->attribute_id = attribute_id;
    if (attribute_name)
    {
        strncpy(attr->attribute_name, attribute_name, sizeof(attr->attribute_name) - 1);
    }
    cluster->attributes_count++;
    return attr;
}

#define MAX_ATTRS_PER_CLUSTER 8
typedef struct
{
    uint32_t cluster_id;
    uint16_t attr_ids[MAX_ATTRS_PER_CLUSTER];
    uint8_t attr_count;
} ClusterHandler;

// таблица кластеров и их атрибутов для автоподписки
static const ClusterHandler cluster_handlers[] = {
    // Power Configuration (0x0001)
    {0x0001, {0x0020, 0x0021}, 2}, // BatteryVoltage, BatteryPercentage
    // Temperature Configuration (0x0002)
    {0x0002, {0x0000}, 1}, // DeviceTemperature
    // On/Off (0x0006)
    {0x0006, {0x0000}, 1}, // Status
    // Level Control (0x0008)
    {0x0008, {0x0000}, 1}, // Level
    // Analog Input (0x000C)
    {0x000C, {0x0055}, 1}, // AnalogInput
    // Analog Output (0x000D)
    {0x000D, {0x0055}, 1}, // AnalogOutput
    // Window Covering (0x0102)
    {0x0102, {0x0008}, 1}, // CoverPosition
    // Thermostat (0x0201)
    {0x0201, {0x0000, 0x0012}, 2}, // LocalTemperature, OccupiedHeatingSetpoint
    // Color Control (0x0300)
    {0x0300, {0x0000, 0x0001, 0x0003, 0x0004, 0x0007, 0x0008}, 6}, // CurrentHue, CurrentSaturation, CurrentX, CurrentY, ColorTemperature,ColorMode
    // Illuminance Measurement (0x0400)
    {0x0400, {0x0000, 0xF001, 0xF002}, 3}, // MeasuredValue, EventsPerMinute, DosePerHour
    // Temperature Measurement (0x0402)
    {0x0402, {0x0000}, 1}, // MeasuredValue
    // Pressure Measurement (0x0403)
    {0x0403, {0x0000}, 1}, // MeasuredValue
    // Flow Measurement (0x0404)
    {0x0404, {0x0000}, 1}, // MeasuredValue
    // Humidity Measurement (0x0405)
    {0x0405, {0x0000}, 1}, // MeasuredValue
    // Occupancy Sensing (0x0406)
    {0x0406, {0x0000}, 1}, // Occupancy
    // Moisture Measurement (0x0407)
    {0x0407, {0x0000}, 1}, // MeasuredValue
    // CO2 Concentration (0x040C)
    {0x040C, {0x0000}, 1}, // MeasuredValue
    // PM2.5 Concentration (0x042A)
    {0x042A, {0x0000, 0x00C8, 0x00C9}, 3}, // MeasuredValue, PM1.0, PM10
    // Smart Energy Metering (0x0702)
    {0x0702, {0x0000, 0x0100, 0x0102, 0x0104, 0x0106}, 5}, // CurrentSummationDelivered, T1, T2, T3, T4
    // Electrical Measurement (0x0B04)
    {0x0B04, {0x0505, 0x0508, 0x050B}, 3}, // RMSVoltage, RMSCurrent, ActivePower
    // Perenio Custom (0xFC00)
    {0xFC00, {0x0003, 0x000A, 0x000E}, 3} // Voltage, Power, Energy
};

// Обработка отчета об атрибуте
/**
 * @brief Обработка отчета об атрибуте
 *
 * @param controller Указатель на структуру контроллера
 * @param node_id Идентификатор узла
 * @param endpoint_id Идентификатор endpoint
 * @param cluster_id Идентификатор кластера
 * @param attribute_id Идентификатор атрибута (0 для пропуска атрибутов)
 * @param value Указатель на значение атрибута (nullptr для пропуска обновления)
 */
void handle_attribute_report(matter_controller_t *controller, uint64_t node_id,
                             uint16_t endpoint_id, uint32_t cluster_id,
                             uint32_t attribute_id, esp_matter_attr_val_t *value, std::optional<bool> need_subscribe)
{
    // Проверка валидности указателя контроллера
    //    if (!controller || controller->magic != CONTROLLER_MAGIC)
    if (!controller)
    {
        ESP_LOGE(TAG_device, "Invalid controller pointer");
        return;
    }

    // Находим или создаем узел (если node_id валиден)
    matter_device_t *node = find_node(controller, node_id);
    if (!node)
    {
        node = add_node(controller, node_id, "Unknown Model", "Unknown Vendor");
        if (!node)
        {
            ESP_LOGE(TAG_device, "Failed to create node 0x%016llX", node_id);
            return;
        }
        ESP_LOGI(TAG_device, "Created new node: 0x%016llX", node_id);
    }

    // Обработка endpoint (если endpoint_id валиден)
    endpoint_entry_t *endpoint = NULL;
    for (uint16_t i = 0; i < node->endpoints_count; i++)
    {
        if (node->endpoints[i].endpoint_id == endpoint_id)
        {
            endpoint = &node->endpoints[i];
            break;
        }
    }
    if (!endpoint)
    {
        endpoint = add_endpoint(node, endpoint_id, NULL);
        if (!endpoint)
        {
            ESP_LOGE(TAG_device, "Failed to add endpoint %d to node 0x%016llX",
                     endpoint_id, node_id);
            return;
        }
        ESP_LOGI(TAG_device, "Added endpoint %d to node 0x%016llX", endpoint_id, node_id);
    }

    // Если cluster_id = 0, пропускаем обработку кластера
    if (cluster_id == 0)
    {
        //  ESP_LOGW(TAG_device, "Cluster ID is 0, skipping cluster processing");
        return;
    }

    // Обработка кластера (серверного по умолчанию)
    bool is_client = false;
    matter_cluster_t *cluster = NULL;

    // Проверяем существующие кластеры
    for (uint16_t i = 0; i < node->server_clusters_count; i++)
    {
        if (node->server_clusters[i].cluster_id == cluster_id)
        {
            cluster = &node->server_clusters[i];
            break;
        }
    }

    // Если кластер не найден, создаем новый
    if (!cluster)
    {

        cluster = add_cluster(node, cluster_id, ClusterIdToText(cluster_id), is_client);
        if (!cluster)
        {
            ESP_LOGE(TAG_device, "Failed to add cluster 0x%04X", cluster_id);
            return;
        }
        ESP_LOGI(TAG_device, "Added new cluster 0x%04X (%s)", cluster_id, ClusterIdToText(cluster_id));
    }

    // Если attribute_id = 0, пропускаем обработку атрибутов
    if (attribute_id == 0x9999)
    {
        //  ESP_LOGW(TAG_device, "Attribute ID NULL, skipping attribute processing");
        return;
    }

    // Поиск существующего атрибута
    matter_attribute_t *attribute = NULL;
    for (uint16_t i = 0; i < cluster->attributes_count; i++)
    {
        if (cluster->attributes[i].attribute_id == attribute_id)
        {
            attribute = &cluster->attributes[i];
            break;
        }
    }

    // Если атрибут не найден, создаем новый
    if (!attribute)
    {
        attribute = add_attribute(cluster, attribute_id, AttributeIdToText(cluster_id, attribute_id));
        if (!attribute)
        {
            ESP_LOGE(TAG_device, "Failed to add attribute 0x%04X", attribute_id);
            return;
        }
        ESP_LOGI(TAG_device, "Added new attribute 0x%04X (%s)", attribute_id, AttributeIdToText(cluster_id, attribute_id));
        // Для нового атрибута устанавливаем флаг подписки по умолчанию
        const ClusterHandler *handler = nullptr;
        for (size_t i = 0; i < sizeof(cluster_handlers) / sizeof(ClusterHandler); i++)
        {
            if (cluster_handlers[i].cluster_id == cluster_id)
            {
                handler = &cluster_handlers[i];
                break;
            }
        }

        if (handler)
        {
            bool found = false;
            for (uint8_t j = 0; j < handler->attr_count; j++)
            {
                if (handler->attr_ids[j] == attribute_id)
                {
                    found = true;
                    break;
                }
            }
            attribute->subscribe = found;
        }
        else
        {
            attribute->subscribe = false;
        }

        // Сохраняем изменения в NVS если нашли новый атрибут
        // возможно сохранение надо перенести кудато-то когда опрос устройства уже закончен
        esp_err_t save_err = save_devices_to_nvs(&g_controller);
        if (save_err != ESP_OK)
        {
            ESP_LOGE(TAG_device, "Failed to save devices: 0x%x", save_err);
        }

        //  ESP_LOGI(TAG_device, "Device 0x%016llX successfully saved", node_id);
    }

    // Если атрибут уже существует, проверяем, нужно ли обновлять его значение
    // если указано значение need_subscribe устанавливаем флаг подписки на атрибут если значение в функцию не передано то не меняем флаг подписки
    if (need_subscribe.has_value())
    {
        if (*need_subscribe)
        {
            attribute->subscribe = true;
            ESP_LOGI(TAG_device, "set flag Subscribed to attribute 0x%04X (%s)", attribute_id, AttributeIdToText(cluster_id, attribute_id));
        }
        else
        {
            attribute->subscribe = false;
            ESP_LOGI(TAG_device, "set flag Unsubscribed from attribute 0x%04X (%s)", attribute_id, AttributeIdToText(cluster_id, attribute_id));
        }
    }
    // Если value = nullptr, пропускаем обновление значения
    if (!value)
    {
        //  ESP_LOGW(TAG_device, "Attribute value is NULL, skipping update");
        return;
    }
    // Защита для строковых типов: не обновлять, если строка пуста или указатель NULL
    // if ((value->type == ESP_MATTER_VAL_TYPE_CHAR_STRING || value->type == ESP_MATTER_VAL_TYPE_OCTET_STRING) &&
    //    (value->val.a.b == nullptr || value->val.a.s == 0))
    //{
    //    ESP_LOGW(TAG_device, "Skip update: empty string or null pointer for attribute 0x%04X", attribute_id);
    //    return;
    //}
    // если пришли данные значит подписка уже произошла
    if (attribute->subscribe == true && !attribute->is_subscribed)
    {
        attribute->is_subscribed = true;
        //    ESP_LOGI(TAG_device,
        //             "successful subscribed: node: %" PRIu64 ", Endpoint: %u, Cluster (%s): 0x%" PRIx32 ", Attribute (%s): 0x%" PRIx32,
        //             node,
        //             endpoint_id,
        //             ClusterIdToText(cluster_id) ? ClusterIdToText(cluster_id) : "Unknown",
        //             cluster_id,
        //             AttributeIdToText(cluster_id, attribute_id) ? AttributeIdToText(cluster_id, attribute_id) : "Unknown",
        //             attribute_id);
    }
    // Обновляем значение атрибута
    memcpy(&attribute->current_value, value, sizeof(esp_matter_attr_val_t));
    publish_fd(&g_controller, node_id, endpoint_id, cluster_id, attribute_id);
}

esp_err_t remove_device(matter_controller_t *controller, uint64_t node_id)
{
    // if (!controller || controller->magic != CONTROLLER_MAGIC)
    if (!controller)
    {
        ESP_LOGE(TAG_device, "Invalid controller pointer");
        return ESP_ERR_INVALID_ARG;
    }

    matter_device_t *current = controller->nodes_list;
    matter_device_t *prev = NULL;
    bool found = false;

    // Поиск устройства в списке
    while (current)
    {
        if (current->node_id == node_id)
        {
            found = true;
            break;
        }
        prev = current;
        current = current->next;
    }

    if (!found)
    {
        ESP_LOGE(TAG_device, "Device 0x%016llX not found", node_id);
        return ESP_ERR_NOT_FOUND;
    }

    // Удаление из списка
    if (prev)
    {
        prev->next = current->next;
    }
    else
    {
        controller->nodes_list = current->next;
    }

    // Очистка ресурсов устройства
    ESP_LOGI(TAG_device, "Removing device 0x%016llX", node_id);

    // 1. Удаляем endpoint'ы
    if (current->endpoints)
    {
        free(current->endpoints);
    }

    // 2. Удаляем серверные кластеры
    for (uint16_t i = 0; i < current->server_clusters_count; i++)
    {
        if (current->server_clusters[i].attributes)
        {
            free(current->server_clusters[i].attributes);
        }
    }
    if (current->server_clusters)
    {
        free(current->server_clusters);
    }

    // 3. Удаляем клиентские кластеры
    for (uint16_t i = 0; i < current->client_clusters_count; i++)
    {
        if (current->client_clusters[i].attributes)
        {
            free(current->client_clusters[i].attributes);
        }
    }
    if (current->client_clusters)
    {
        free(current->client_clusters);
    }

    // 4. Освобождаем сам узел
    free(current);
    controller->nodes_count--;

    // Сохраняем изменения в NVS
    esp_err_t save_err = save_devices_to_nvs(controller);
    if (save_err != ESP_OK)
    {
        ESP_LOGE(TAG_device, "Failed to save devices after removal: 0x%x", save_err);
        return save_err;
    }

    ESP_LOGI(TAG_device, "Device 0x%016llX successfully removed", node_id);
    return ESP_OK;
}

// Освобождение памяти
void matter_controller_free(matter_controller_t *controller)
{
    matter_device_t *current = controller->nodes_list;
    while (current != NULL)
    {
        matter_device_t *next = current->next;

        // Освобождаем endpoints
        if (current->endpoints)
            free(current->endpoints);

        // Освобождаем серверные кластеры
        for (uint16_t i = 0; i < current->server_clusters_count; i++)
        {
            if (current->server_clusters[i].attributes)
            {
                free(current->server_clusters[i].attributes);
            }
        }
        if (current->server_clusters)
            free(current->server_clusters);

        // Освобождаем клиентские кластеры
        for (uint16_t i = 0; i < current->client_clusters_count; i++)
        {
            if (current->client_clusters[i].attributes)
            {
                free(current->client_clusters[i].attributes);
            }
        }
        if (current->client_clusters)
            free(current->client_clusters);

        free(current);
        current = next;
    }
    controller->nodes_list = NULL;
    controller->nodes_count = 0;
}

const char *attr_val_to_char_str(const esp_matter_attr_val_t *val, char *buf, size_t buf_size)
{
    if (!val || !buf || buf_size == 0)
    {
        return "";
    }

    switch (val->type)
    {
    case ESP_MATTER_VAL_TYPE_BOOLEAN:
        snprintf(buf, buf_size, "%s", val->val.b ? "true" : "false");
        break;
    case ESP_MATTER_VAL_TYPE_INT32:
        snprintf(buf, buf_size, "%d", val->val.i32);
        break;
    case ESP_MATTER_VAL_TYPE_UINT32:
        snprintf(buf, buf_size, "%u", val->val.u32);
        break;
    case ESP_MATTER_VAL_TYPE_INT64:
        snprintf(buf, buf_size, "%lld", (long long)val->val.i64);
        break;
    case ESP_MATTER_VAL_TYPE_UINT64:
        snprintf(buf, buf_size, "%llu", (unsigned long long)val->val.u64);
        break;
    case ESP_MATTER_VAL_TYPE_FLOAT:
        snprintf(buf, buf_size, "%f", val->val.f);
        break;
    case ESP_MATTER_VAL_TYPE_CHAR_STRING:
        snprintf(buf, buf_size, "\"%.*s\"", (int)val->val.a.s, (const char *)val->val.a.b);
        break;
    case ESP_MATTER_VAL_TYPE_OCTET_STRING:
        snprintf(buf, buf_size, "\"<octet_string_len_%d>\"", (int)val->val.a.s);
        break;
    default:
        snprintf(buf, buf_size, "\"unsupported\"");
        break;
    }
    return buf;
}

esp_err_t publish_fd(matter_controller_t *controller, uint64_t node_id,
                     uint16_t endpoint_id, uint32_t cluster_id,
                     uint32_t attribute_id)
{
    if (!controller)
        return ESP_ERR_INVALID_ARG;

#define MAX_TOPIC_LEN 256
#define MAX_MSG_LEN 2048
    char fdTopic[MAX_TOPIC_LEN];
    char value_str[64]; // Буфер для значений атрибутов

    matter_device_t *node = controller->nodes_list;
    while (node)
    {
        // Проверяем node_id
        if (node->node_id != node_id)
        {
            node = node->next;
            continue;
        }

        cJSON *root = cJSON_CreateObject();
        bool has_data = false;

        for (uint16_t ep_idx = 0; ep_idx < node->endpoints_count; ++ep_idx)
        {
            endpoint_entry_t *ep = &node->endpoints[ep_idx];

            // Проверяем endpoint_id
            if (ep->endpoint_id != endpoint_id)
            {
                continue;
            }

            for (uint8_t cl_idx = 0; cl_idx < ep->cluster_count; ++cl_idx)
            {
                uint32_t current_cluster_id = ep->clusters[cl_idx];

                // Поиск кластера среди server_clusters
                for (uint16_t s = 0; s < node->server_clusters_count; ++s)
                {
                    matter_cluster_t *cluster = &node->server_clusters[s];
                    if (cluster->cluster_id == current_cluster_id && cluster->attributes)
                    {
                        const char *cluster_name = ClusterIdToText((chip::ClusterId)cluster->cluster_id);
                        cJSON *cluster_obj = NULL;

                        for (uint16_t a = 0; a < cluster->attributes_count; ++a)
                        {
                            matter_attribute_t *attr = &cluster->attributes[a];

                            if (attr->current_value.type)
                            {
                                if (!cluster_obj)
                                {
                                    // Создаем объект кластера
                                    cluster_obj = cJSON_GetObjectItemCaseSensitive(root, cluster_name);
                                    if (!cluster_obj)
                                    {
                                        cluster_obj = cJSON_CreateObject();
                                        cJSON_AddItemToObject(root, cluster_name, cluster_obj);
                                    }
                                    has_data = true;
                                }

                                // Получаем название атрибута
                                const char *attr_name = AttributeIdToText(
                                    (chip::ClusterId)cluster->cluster_id,
                                    (chip::AttributeId)attr->attribute_id);

                                // Преобразуем значение в строку
                                attr_val_to_char_str(&attr->current_value, value_str, sizeof(value_str));

                                // Добавляем в JSON в зависимости от типа
                                switch (attr->current_value.type)
                                {
                                case ESP_MATTER_VAL_TYPE_BOOLEAN:
                                    cJSON_AddBoolToObject(cluster_obj, attr_name, attr->current_value.val.b);
                                    break;
                                case ESP_MATTER_VAL_TYPE_INT32:
                                    cJSON_AddNumberToObject(cluster_obj, attr_name, attr->current_value.val.i32);
                                    break;
                                case ESP_MATTER_VAL_TYPE_UINT32:
                                    cJSON_AddNumberToObject(cluster_obj, attr_name, attr->current_value.val.u32);
                                    break;
                                case ESP_MATTER_VAL_TYPE_INT64:
                                    cJSON_AddNumberToObject(cluster_obj, attr_name, (double)attr->current_value.val.i64);
                                    break;
                                case ESP_MATTER_VAL_TYPE_UINT64:
                                    cJSON_AddNumberToObject(cluster_obj, attr_name, (double)attr->current_value.val.u64);
                                    break;
                                case ESP_MATTER_VAL_TYPE_FLOAT:
                                    cJSON_AddNumberToObject(cluster_obj, attr_name, attr->current_value.val.f);
                                    break;
                                case ESP_MATTER_VAL_TYPE_CHAR_STRING:
                                    cJSON_AddStringToObject(cluster_obj, attr_name, (const char *)attr->current_value.val.a.b);
                                    break;
                                default:
                                    // Для остальных типов используем строковое представление
                                    cJSON_AddStringToObject(cluster_obj, attr_name, value_str);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Публикуем данные, если они есть
        if (has_data)
        {
            const char *mqttPrefix = sys_settings.mqtt.prefix;
            snprintf(fdTopic, MAX_TOPIC_LEN, "%s/fd/matter_csa_name/%llu/%u", mqttPrefix, node->node_id, endpoint_id);
            char *json_str = cJSON_PrintUnformatted(root);
            if (json_str)
            {
                if (strlen(json_str) < MAX_MSG_LEN)
                {
                    mqtt_publish_data(fdTopic, json_str);
                }
                else
                {
                    ESP_LOGE("MQTT", "Message too large for topic %s", fdTopic);
                }
                free(json_str);
            }
        }

        cJSON_Delete(root);
        node = node->next;
    }
    return ESP_OK;
}

// Колбэк для успешной подписки
void subscribe_done(uint64_t node_id, uint32_t subscription_id)
{
    ESP_LOGI(TAG_device, "Successfully subscribed, node %llu, subscription id 0x%08X", node_id, subscription_id);
}

// Колбэк для неудачной подписки
void subscribe_failed(void *ctx)
{
    ESP_LOGE(TAG_device, "Failed to subscribe (context: %p)", ctx);
}

esp_err_t subscribe_all_marked_attributes(matter_controller_t *controller)
{
    if (!controller)
        return ESP_ERR_INVALID_ARG;

    struct SubscriptionKey
    {
        uint64_t node_id;
        uint16_t endpoint_id;
        uint32_t cluster_id;
        uint32_t attribute_id;
        bool operator<(const SubscriptionKey &other) const
        {
            return std::tie(node_id, endpoint_id, cluster_id, attribute_id) <
                   std::tie(other.node_id, other.endpoint_id, other.cluster_id, other.attribute_id);
        }
    };

    std::set<SubscriptionKey> subscribed;

    matter_device_t *node = controller->nodes_list;
    while (node)
    {
        for (uint16_t ep_idx = 0; ep_idx < node->endpoints_count; ++ep_idx)
        {
            endpoint_entry_t *ep = &node->endpoints[ep_idx];
            for (uint8_t cl_idx = 0; cl_idx < ep->cluster_count; ++cl_idx)
            {
                uint32_t cluster_id = ep->clusters[cl_idx];
                for (uint16_t s = 0; s < node->server_clusters_count; ++s)
                {
                    matter_cluster_t *cluster = &node->server_clusters[s];
                    if (cluster->cluster_id == cluster_id && cluster->attributes)
                    {
                        for (uint16_t a = 0; a < cluster->attributes_count; ++a)
                        {
                            matter_attribute_t *attr = &cluster->attributes[a];
                            if (attr->subscribe)
                            {
                                SubscriptionKey key{node->node_id, ep->endpoint_id, cluster->cluster_id, attr->attribute_id};
                                if (subscribed.find(key) != subscribed.end())
                                    continue; // Уже подписались

                                subscribed.insert(key);

                                ESP_LOGI(TAG_device, "Subscribing to attribute: %s (0x%04X) on cluster: %s (0x%04X) for node %llu, endpoint %d",
                                         AttributeIdToText(cluster->cluster_id, attr->attribute_id), attr->attribute_id, ClusterIdToText(cluster->cluster_id), cluster->cluster_id, node->node_id, ep->endpoint_id);

                                uint16_t min_interval = 0;
                                uint16_t max_interval = 60;

                                auto *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(
                                    node->node_id, ep->endpoint_id, cluster->cluster_id, attr->attribute_id,
                                    esp_matter::controller::SUBSCRIBE_ATTRIBUTE, min_interval, max_interval, true,
                                    OnAttributeData, nullptr, subscribe_done, subscribe_failed);
                                if (!cmd)
                                {
                                    ESP_LOGE(TAG_device, "Failed to alloc memory for subscribe_command");
                                }
                                else
                                {
                                    esp_err_t err = cmd->send_command();
                                    if (err != ESP_OK)
                                    {
                                        ESP_LOGE(TAG_device, "Failed to send subscribe command: %s", esp_err_to_name(err));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        node = node->next;
    }
    return ESP_OK;
}

// ЛОГ с информацией о кластере и его атрибутах
void log_cluster_info(const matter_cluster_t *cluster, bool is_client)
{
    if (!cluster)
    {
        ESP_LOGW(TAG_device, "log_cluster_info: cluster is NULL");
        return;
    }

    ESP_LOGI(TAG_device, "  %s Cluster: %lu (%lx) '%s'",
             is_client ? "Client" : "Server",
             cluster->cluster_id, cluster->cluster_id,
             cluster->cluster_name[0] ? cluster->cluster_name : "unnamed");

    for (uint16_t i = 0; i < cluster->attributes_count; i++)
    {
        const matter_attribute_t *attr = &cluster->attributes[i];
        if (!attr)
            continue;

        const char *attr_name = (attr->attribute_name[0]) ? attr->attribute_name : "unnamed";
        ESP_LOGI(TAG_device, "    Attribute: 0x%04x '%s' - Subscribe: %s",
                 attr->attribute_id,
                 attr_name,
                 attr->subscribe ? "✅" : "➖");

        switch (attr->current_value.type)
        {
        case ESP_MATTER_VAL_TYPE_BOOLEAN:
            ESP_LOGI(TAG_device, "      Value: %s", attr->current_value.val.b ? "true" : "false");
            break;
        case ESP_MATTER_VAL_TYPE_INTEGER:
            ESP_LOGI(TAG_device, "      Value: %d", attr->current_value.val.i8);
            break;
        case ESP_MATTER_VAL_TYPE_FLOAT:
            ESP_LOGI(TAG_device, "      Value: %f", attr->current_value.val.f);
            break;
        case ESP_MATTER_VAL_TYPE_CHAR_STRING:
            if (attr->current_value.val.a.b)
                ESP_LOGI(TAG_device, "      Value: %.*s", attr->current_value.val.a.s, attr->current_value.val.a.b);
            else
                ESP_LOGI(TAG_device, "      Value: [empty string]");
            break;
        case ESP_MATTER_VAL_TYPE_OCTET_STRING:
            ESP_LOGI(TAG_device, "      Value: [octet string, len %d]", attr->current_value.val.a.n);
            break;
        default:
            ESP_LOGI(TAG_device, "      Value: [type 0x%02x]", attr->current_value.type);
        }
    }
}

void log_node_info(const matter_device_t *node)
{
    if (!node)
        return;

    ESP_LOGI(TAG_device, "Node: %llu", node->node_id);
    ESP_LOGI(TAG_device, "  Model: %s, Vendor: %s", node->model_name, node->vendor_name);
    ESP_LOGI(TAG_device, "  Status: %s", node->is_online ? "online" : "offline");
    ESP_LOGI(TAG_device, "  Firmware: %s", node->firmware_version);

    // Логирование endpoint'ов
    for (uint16_t i = 0; i < node->endpoints_count; i++)
    {
        const endpoint_entry_t *ep = &node->endpoints[i];
        ESP_LOGI(TAG_device, "  Endpoint: %d ", ep->endpoint_id);

        // Логирование серверных кластеров
        for (uint16_t i = 0; i < node->server_clusters_count; i++)
        {
            log_cluster_info(&node->server_clusters[i], false);
        }

        // Логирование клиентских кластеров
        for (uint16_t i = 0; i < node->client_clusters_count; i++)
        {
            log_cluster_info(&node->client_clusters[i], true);
        }
    }
}

void log_controller_structure(const matter_controller_t *controller)
{
    if (!controller)
        return;

    ESP_LOGI(TAG_device, "===== Matter Controller Structure =====");
    ESP_LOGI(TAG_device, "Controller Node ID: 0x%016llx", controller->controller_node_id);
    ESP_LOGI(TAG_device, "Fabric ID: %d", controller->fabric_id);
    ESP_LOGI(TAG_device, "Connected nodes: %d", controller->nodes_count);

    const matter_device_t *node = controller->nodes_list;
    while (node)
    {
        log_node_info(node);
        node = node->next;
        if (node)
            ESP_LOGI(TAG_device, "-----------------------");
    }

    ESP_LOGI(TAG_device, "===== End of Structure =====");
}

// --- Сохранение устройств в NVS с полной структурой ---
esp_err_t save_devices_to_nvs(matter_controller_t *controller)
{
    if (!controller)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    // --- Подсчет размера ---
    size_t required_size = sizeof(uint16_t); // nodes_count
    matter_device_t *current = controller->nodes_list;
    while (current)
    {
        required_size += sizeof(uint64_t) + sizeof(bool) + 32 + 64 + 32 + sizeof(uint32_t) + 32 + sizeof(uint16_t);
        required_size += sizeof(uint16_t); // endpoints_count
        for (uint16_t e = 0; e < current->endpoints_count; e++)
        {
            required_size += sizeof(uint16_t) + 32 + sizeof(uint8_t) + sizeof(uint16_t) * 16;
        }
        required_size += sizeof(uint16_t); // server_clusters_count
        for (uint16_t sc = 0; sc < current->server_clusters_count; sc++)
        {
            required_size += sizeof(uint32_t) + 32 + sizeof(bool) + sizeof(uint16_t);
            for (uint16_t a = 0; a < current->server_clusters[sc].attributes_count; a++)
            {
                required_size += sizeof(uint32_t) + 32 + sizeof(bool);
            }
        }
        required_size += sizeof(uint16_t); // client_clusters_count
        for (uint16_t cc = 0; cc < current->client_clusters_count; cc++)
        {
            required_size += sizeof(uint32_t) + 32 + sizeof(bool) + sizeof(uint16_t);
            for (uint16_t a = 0; a < current->client_clusters[cc].attributes_count; a++)
            {
                required_size += sizeof(uint32_t) + 32 + sizeof(bool);
            }
        }
        current = current->next;
    }

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *ptr = buffer;

    *((uint16_t *)ptr) = controller->nodes_count;
    ptr += sizeof(uint16_t);

    current = controller->nodes_list;
    while (current)
    {
        *((uint64_t *)ptr) = current->node_id;
        ptr += sizeof(uint64_t);
        *((bool *)ptr) = current->is_online;
        ptr += sizeof(bool);
        memcpy(ptr, current->model_name, 32);
        ptr += 32;
        memcpy(ptr, current->description, 64);
        ptr += 64;
        memcpy(ptr, current->vendor_name, 32);
        ptr += 32;
        *((uint32_t *)ptr) = current->vendor_id;
        ptr += sizeof(uint32_t);
        memcpy(ptr, current->firmware_version, 32);
        ptr += 32;
        *((uint16_t *)ptr) = current->product_id;
        ptr += sizeof(uint16_t);

        // endpoints
        *((uint16_t *)ptr) = current->endpoints_count;
        ptr += sizeof(uint16_t);
        for (uint16_t e = 0; e < current->endpoints_count; e++)
        {
            endpoint_entry_t *ep = &current->endpoints[e];
            *((uint16_t *)ptr) = ep->endpoint_id;
            ptr += sizeof(uint16_t);
            memcpy(ptr, ep->endpoint_name, 32);
            ptr += 32;
            *((uint8_t *)ptr) = ep->cluster_count;
            ptr += sizeof(uint8_t);
            memcpy(ptr, ep->clusters, sizeof(uint16_t) * 16);
            ptr += sizeof(uint16_t) * 16;
        }

        // server_clusters
        *((uint16_t *)ptr) = current->server_clusters_count;
        ptr += sizeof(uint16_t);
        for (uint16_t sc = 0; sc < current->server_clusters_count; sc++)
        {
            matter_cluster_t *cl = &current->server_clusters[sc];
            *((uint32_t *)ptr) = cl->cluster_id;
            ptr += sizeof(uint32_t);
            memcpy(ptr, cl->cluster_name, 32);
            ptr += 32;
            *((bool *)ptr) = cl->is_client;
            ptr += sizeof(bool);
            *((uint16_t *)ptr) = cl->attributes_count;
            ptr += sizeof(uint16_t);
            for (uint16_t a = 0; a < cl->attributes_count; a++)
            {
                matter_attribute_t *attr = &cl->attributes[a];
                *((uint32_t *)ptr) = attr->attribute_id;
                ptr += sizeof(uint32_t);
                memcpy(ptr, attr->attribute_name, 32);
                ptr += 32;
                *((bool *)ptr) = attr->subscribe;
                ptr += sizeof(bool);
            }
        }

        // client_clusters
        *((uint16_t *)ptr) = current->client_clusters_count;
        ptr += sizeof(uint16_t);
        for (uint16_t cc = 0; cc < current->client_clusters_count; cc++)
        {
            matter_cluster_t *cl = &current->client_clusters[cc];
            *((uint32_t *)ptr) = cl->cluster_id;
            ptr += sizeof(uint32_t);
            memcpy(ptr, cl->cluster_name, 32);
            ptr += 32;
            *((bool *)ptr) = cl->is_client;
            ptr += sizeof(bool);
            *((uint16_t *)ptr) = cl->attributes_count;
            ptr += sizeof(uint16_t);
            for (uint16_t a = 0; a < cl->attributes_count; a++)
            {
                matter_attribute_t *attr = &cl->attributes[a];
                *((uint32_t *)ptr) = attr->attribute_id;
                ptr += sizeof(uint32_t);
                memcpy(ptr, attr->attribute_name, 32);
                ptr += 32;
                *((bool *)ptr) = attr->subscribe;
                ptr += sizeof(bool);
            }
        }

        current = current->next;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY, buffer, required_size);
    free(buffer);

    if (err == ESP_OK)
        err = nvs_commit(nvs_handle);

    nvs_close(nvs_handle);
    return err;
}

// --- Загрузка устройств из NVS с полной структурой ---
esp_err_t load_devices_from_nvs(matter_controller_t *controller)
{
    if (!controller)
        return ESP_ERR_INVALID_ARG;

    // Очищаем старый список устройств перед загрузкой новых
    matter_controller_free(controller);

    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
        return err;

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, NVS_KEY, NULL, &required_size);
    if (err != ESP_OK || required_size == 0)
    {
        nvs_close(nvs_handle);
        return err;
    }

    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_blob(nvs_handle, NVS_KEY, buffer, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        free(buffer);
        return err;
    }

    uint8_t *ptr = buffer;
    uint16_t nodes_count = *((uint16_t *)ptr);
    ptr += sizeof(uint16_t);

    for (uint16_t i = 0; i < nodes_count; i++)
    {
        matter_device_t *node = (matter_device_t *)calloc(1, sizeof(matter_device_t));
        if (!node)
        {
            free(buffer);
            return ESP_ERR_NO_MEM;
        }

        node->node_id = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);
        node->is_online = *((bool *)ptr);
        ptr += sizeof(bool);
        memcpy(node->model_name, ptr, 32);
        ptr += 32;
        memcpy(node->description, ptr, 64);
        ptr += 64;
        memcpy(node->vendor_name, ptr, 32);
        ptr += 32;
        node->vendor_id = *((uint32_t *)ptr);
        ptr += sizeof(uint32_t);
        memcpy(node->firmware_version, ptr, 32);
        ptr += 32;
        node->product_id = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        // endpoints
        node->endpoints_count = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);
        if (node->endpoints_count > 0)
        {
            node->endpoints = (endpoint_entry_t *)calloc(node->endpoints_count, sizeof(endpoint_entry_t));
            for (uint16_t e = 0; e < node->endpoints_count; e++)
            {
                endpoint_entry_t *ep = &node->endpoints[e];
                ep->endpoint_id = *((uint16_t *)ptr);
                ptr += sizeof(uint16_t);
                memcpy(ep->endpoint_name, ptr, 32);
                ptr += 32;
                ep->cluster_count = *((uint8_t *)ptr);
                ptr += sizeof(uint8_t);
                memcpy(ep->clusters, ptr, sizeof(uint16_t) * 16);
                ptr += sizeof(uint16_t) * 16;
            }
        }

        // server_clusters
        node->server_clusters_count = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);
        if (node->server_clusters_count > 0)
        {
            node->server_clusters = (matter_cluster_t *)calloc(node->server_clusters_count, sizeof(matter_cluster_t));
            for (uint16_t sc = 0; sc < node->server_clusters_count; sc++)
            {
                matter_cluster_t *cl = &node->server_clusters[sc];
                cl->cluster_id = *((uint32_t *)ptr);
                ptr += sizeof(uint32_t);
                memcpy(cl->cluster_name, ptr, 32);
                ptr += 32;
                cl->is_client = *((bool *)ptr);
                ptr += sizeof(bool);
                cl->attributes_count = *((uint16_t *)ptr);
                ptr += sizeof(uint16_t);
                if (cl->attributes_count > 0)
                {
                    cl->attributes = (matter_attribute_t *)calloc(cl->attributes_count, sizeof(matter_attribute_t));
                    for (uint16_t a = 0; a < cl->attributes_count; a++)
                    {
                        matter_attribute_t *attr = &cl->attributes[a];
                        attr->attribute_id = *((uint32_t *)ptr);
                        ptr += sizeof(uint32_t);
                        memcpy(attr->attribute_name, ptr, 32);
                        ptr += 32;
                        attr->subscribe = *((bool *)ptr);
                        ptr += sizeof(bool);
                    }
                }
            }
        }

        // client_clusters
        node->client_clusters_count = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);
        if (node->client_clusters_count > 0)
        {
            node->client_clusters = (matter_cluster_t *)calloc(node->client_clusters_count, sizeof(matter_cluster_t));
            for (uint16_t cc = 0; cc < node->client_clusters_count; cc++)
            {
                matter_cluster_t *cl = &node->client_clusters[cc];
                cl->cluster_id = *((uint32_t *)ptr);
                ptr += sizeof(uint32_t);
                memcpy(cl->cluster_name, ptr, 32);
                ptr += 32;
                cl->is_client = *((bool *)ptr);
                ptr += sizeof(bool);
                cl->attributes_count = *((uint16_t *)ptr);
                ptr += sizeof(uint16_t);
                if (cl->attributes_count > 0)
                {
                    cl->attributes = (matter_attribute_t *)calloc(cl->attributes_count, sizeof(matter_attribute_t));
                    for (uint16_t a = 0; a < cl->attributes_count; a++)
                    {
                        matter_attribute_t *attr = &cl->attributes[a];
                        attr->attribute_id = *((uint32_t *)ptr);
                        ptr += sizeof(uint32_t);
                        memcpy(attr->attribute_name, ptr, 32);
                        ptr += 32;
                        attr->subscribe = *((bool *)ptr);
                        ptr += sizeof(bool);
                    }
                }
            }
        }

        node->next = controller->nodes_list;
        controller->nodes_list = node;
    }

    controller->nodes_count = nodes_count;
    free(buffer);
    return ESP_OK;
}

// Очистка сохраненных устройств
void clear_devices_in_nvs()
{
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK)
    {
        nvs_erase_key(nvs_handle, NVS_KEY);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
}