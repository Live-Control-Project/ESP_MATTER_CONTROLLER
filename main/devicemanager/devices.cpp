#include "devices.h"
#include <string.h>
#include <stdlib.h>
#include <esp_log.h>

#include <nvs_flash.h>
#include <nvs.h>

#define NVS_NAMESPACE "matter_devices"
#define NVS_KEY "devices_list"

const char *TAG_device = "devices.cpp";

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
}

// Поиск узла по ID
matter_node_t *find_node(matter_controller_t *controller, uint64_t node_id)
{
    matter_node_t *current = controller->nodes_list;
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
matter_node_t *add_node(matter_controller_t *controller, uint64_t node_id, const char *model_name, const char *vendor_name)
{
    matter_node_t *new_node = (matter_node_t *)malloc(sizeof(matter_node_t));
    if (!new_node)
        return NULL;

    memset(new_node, 0, sizeof(matter_node_t));
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
matter_endpoint_t *add_endpoint(matter_node_t *node, uint16_t endpoint_id, const char *endpoint_name)
{
    matter_endpoint_t *new_endpoints = (matter_endpoint_t *)realloc(node->endpoints,
                                                                    (node->endpoints_count + 1) * sizeof(matter_endpoint_t));
    if (!new_endpoints)
        return NULL;

    node->endpoints = new_endpoints;
    matter_endpoint_t *ep = &node->endpoints[node->endpoints_count];
    memset(ep, 0, sizeof(matter_endpoint_t));
    ep->endpoint_id = endpoint_id;
    if (endpoint_name)
    {
        strncpy(ep->endpoint_name, endpoint_name, sizeof(ep->endpoint_name) - 1);
    }
    node->endpoints_count++;
    return ep;
}

// Добавление кластера к узлу
matter_cluster_t *add_cluster(matter_node_t *node, uint32_t cluster_id, const char *cluster_name, bool is_client)
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
                             uint32_t attribute_id, esp_matter_attr_val_t *value)
{
    // Проверка валидности указателя контроллера
    //    if (!controller || controller->magic != CONTROLLER_MAGIC)
    if (!controller)
    {
        ESP_LOGE(TAG_device, "Invalid controller pointer");
        return;
    }

    // Находим или создаем узел (если node_id валиден)
    matter_node_t *node = find_node(controller, node_id);
    if (!node)
    {
        node = add_node(controller, node_id, "Unknown Model", "Unknown Vendor");
        if (!node)
        {
            ESP_LOGE(TAG_device, "Failed to create node 0x%016llX", node_id);
            return;
        }
        ESP_LOGI(TAG_device, "Created new node: 0x%016llX", node_id);
        // save_devices_to_nvs(&controller)
    }

    // Обработка endpoint (если endpoint_id валиден)
    matter_endpoint_t *endpoint = NULL;
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
        ESP_LOGW(TAG_device, "Cluster ID is 0, skipping cluster processing");
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
        cluster = add_cluster(node, cluster_id, NULL, is_client);
        if (!cluster)
        {
            ESP_LOGE(TAG_device, "Failed to add cluster 0x%04X", cluster_id);
            return;
        }
        ESP_LOGI(TAG_device, "Added new cluster 0x%04X", cluster_id);
    }

    // Если attribute_id = 0, пропускаем обработку атрибутов
    if (attribute_id == 0)
    {
        ESP_LOGW(TAG_device, "Attribute ID is 0, skipping attribute processing");
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
        attribute = add_attribute(cluster, attribute_id, NULL);
        if (!attribute)
        {
            ESP_LOGE(TAG_device, "Failed to add attribute 0x%04X", attribute_id);
            return;
        }
        ESP_LOGI(TAG_device, "Added new attribute 0x%04X", attribute_id);
    }

    // Если value = nullptr, пропускаем обновление значения
    if (!value)
    {
        ESP_LOGW(TAG_device, "Attribute value is NULL, skipping update");
        return;
    }

    // Обновляем значение атрибута
    memcpy(&attribute->current_value, value, sizeof(esp_matter_attr_val_t));
    ESP_LOGI(TAG_device, "Updated attribute 0x%04X value", attribute_id);
}

esp_err_t remove_device(matter_controller_t *controller, uint64_t node_id)
{
    // if (!controller || controller->magic != CONTROLLER_MAGIC)
    if (!controller)
    {
        ESP_LOGE(TAG_device, "Invalid controller pointer");
        return ESP_ERR_INVALID_ARG;
    }

    matter_node_t *current = controller->nodes_list;
    matter_node_t *prev = NULL;
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
    matter_node_t *current = controller->nodes_list;
    while (current != NULL)
    {
        matter_node_t *next = current->next;

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

// ЛОГ с информацией о кластере и его атрибутах
void log_cluster_info(const matter_cluster_t *cluster, bool is_client)
{
    if (!cluster)
        return;

    ESP_LOGI(TAG_device, "  %s Cluster: %lu '%s'",
             is_client ? "Client" : "Server",
             cluster->cluster_id,
             cluster->cluster_name[0] ? cluster->cluster_name : "unnamed");

    for (uint16_t i = 0; i < cluster->attributes_count; i++)
    {
        const matter_attribute_t *attr = &cluster->attributes[i];
        ESP_LOGI(TAG_device, "    Attribute: 0x%04x '%s' - Subscribed: %s",
                 attr->attribute_id,
                 attr->attribute_name[0] ? attr->attribute_name : "unnamed",
                 attr->is_subscribed ? "yes" : "no");

        // Логирование значения в зависимости от типа
        switch (attr->current_value.type)
        {
            //        case ESP_MATTER_VAL_TYPE_NULL:
            //            ESP_LOGI(TAG_device, "      Value: NULL");
            //            break;
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
            // ESP_LOGI(TAG_device, "      Value: %s", attr->current_value.val.a.a);
            break;
        case ESP_MATTER_VAL_TYPE_OCTET_STRING:
            ESP_LOGI(TAG_device, "      Value: [octet string, len %d]", attr->current_value.val.a.n);
            break;
        default:
            ESP_LOGI(TAG_device, "      Value: [type 0x%02x]", attr->current_value.type);
        }
    }
}

void log_node_info(const matter_node_t *node)
{
    if (!node)
        return;

    ESP_LOGI(TAG_device, "Node: 0x%016llx", node->node_id);
    ESP_LOGI(TAG_device, "  Model: %s, Vendor: %s", node->model_name, node->vendor_name);
    ESP_LOGI(TAG_device, "  Status: %s", node->is_online ? "online" : "offline");
    ESP_LOGI(TAG_device, "  Firmware: %s", node->firmware_version);

    // Логирование endpoint'ов
    for (uint16_t i = 0; i < node->endpoints_count; i++)
    {
        const matter_endpoint_t *ep = &node->endpoints[i];
        ESP_LOGI(TAG_device, "  Endpoint: %d '%s'", ep->endpoint_id, ep->endpoint_name);
    }

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

void log_controller_structure(const matter_controller_t *controller)
{
    if (!controller)
        return;

    ESP_LOGI(TAG_device, "===== Matter Controller Structure =====");
    ESP_LOGI(TAG_device, "Controller Node ID: 0x%016llx", controller->controller_node_id);
    ESP_LOGI(TAG_device, "Fabric ID: %d", controller->fabric_id);
    ESP_LOGI(TAG_device, "Connected nodes: %d", controller->nodes_count);

    const matter_node_t *node = controller->nodes_list;
    while (node)
    {
        log_node_info(node);
        node = node->next;
        if (node)
            ESP_LOGI(TAG_device, "-----------------------");
    }

    ESP_LOGI(TAG_device, "===== End of Structure =====");
}

// Сохранение устройств в NVS
esp_err_t save_devices_to_nvs(matter_controller_t *controller)
{
    if (!controller)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Открываем NVS пространство
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
        return err;

    // Сериализация данных
    size_t required_size = sizeof(uint16_t); // Для nodes_count
    matter_node_t *current = controller->nodes_list;

    // Вычисляем общий размер данных
    while (current)
    {
        required_size += sizeof(uint64_t) + // node_id
                         sizeof(bool) +     // is_online
                         32 + 32 +          // model_name, vendor_name
                         sizeof(uint32_t) + // vendor_id
                         32 +               // firmware_version
                         sizeof(uint16_t);  // product_id
        current = current->next;
    }

    // Выделяем буфер
    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    // Заполняем буфер
    uint8_t *ptr = buffer;

    // Сохраняем количество устройств
    *((uint16_t *)ptr) = controller->nodes_count;
    ptr += sizeof(uint16_t);

    // Сохраняем каждое устройство
    current = controller->nodes_list;
    while (current)
    {
        *((uint64_t *)ptr) = current->node_id;
        ptr += sizeof(uint64_t);

        *((bool *)ptr) = current->is_online;
        ptr += sizeof(bool);

        memcpy(ptr, current->model_name, 32);
        ptr += 32;

        memcpy(ptr, current->vendor_name, 32);
        ptr += 32;

        *((uint32_t *)ptr) = current->vendor_id;
        ptr += sizeof(uint32_t);

        memcpy(ptr, current->firmware_version, 32);
        ptr += 32;

        *((uint16_t *)ptr) = current->product_id;
        ptr += sizeof(uint16_t);

        current = current->next;
    }

    // Сохраняем в NVS
    err = nvs_set_blob(nvs_handle, NVS_KEY, buffer, required_size);
    free(buffer);

    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

// Загрузка устройств из NVS
esp_err_t load_devices_from_nvs(matter_controller_t *controller)
{
    if (!controller)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Открываем NVS пространство
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
        return err;

    // Получаем размер данных
    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, NVS_KEY, NULL, &required_size);
    if (err != ESP_OK || required_size == 0)
    {
        nvs_close(nvs_handle);
        return err;
    }

    // Выделяем буфер
    uint8_t *buffer = (uint8_t *)malloc(required_size);
    if (!buffer)
    {
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }

    // Читаем данные
    err = nvs_get_blob(nvs_handle, NVS_KEY, buffer, &required_size);
    nvs_close(nvs_handle);

    if (err != ESP_OK)
    {
        free(buffer);
        return err;
    }

    // Десериализация данных
    uint8_t *ptr = buffer;

    // Читаем количество устройств
    uint16_t nodes_count = *((uint16_t *)ptr);
    ptr += sizeof(uint16_t);

    // Восстанавливаем каждое устройство
    for (uint16_t i = 0; i < nodes_count; i++)
    {
        matter_node_t *node = (matter_node_t *)malloc(sizeof(matter_node_t));
        if (!node)
        {
            free(buffer);
            return ESP_ERR_NO_MEM;
        }
        memset(node, 0, sizeof(matter_node_t));

        node->node_id = *((uint64_t *)ptr);
        ptr += sizeof(uint64_t);

        node->is_online = *((bool *)ptr);
        ptr += sizeof(bool);

        memcpy(node->model_name, ptr, 32);
        ptr += 32;

        memcpy(node->vendor_name, ptr, 32);
        ptr += 32;

        node->vendor_id = *((uint32_t *)ptr);
        ptr += sizeof(uint32_t);

        memcpy(node->firmware_version, ptr, 32);
        ptr += 32;

        node->product_id = *((uint16_t *)ptr);
        ptr += sizeof(uint16_t);

        // Добавляем в список
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