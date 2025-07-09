/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <commands/clusters/DataModelLogger.h>
// #include <app_matter.h>
#include <device.h>
#include <esp_check.h>
#include <esp_matter.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_read_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_core.h>
#include <led_driver.h>
#include <matter_controller_device_mgr.h>

#include "app_matter_ctrl.h"

#include <read_node_info.h>
#include "matter_callbacks.h"

/* maintain a local subscribe list for Matter only device */
// Дополненная структура для атрибута в списке подписки
typedef struct subscribed_attribute
{
    uint32_t cluster_id;
    uint32_t attribute_id;
    char attribute_name[32];
    esp_matter_attr_val_t current_value;
    bool subscribe;
    bool is_subscribed;
    void *subscribe_ptr;
    struct subscribed_attribute *next;
} subscribed_attribute_t;

// Дополненная структура для кластера в списке подписки
typedef struct subscribed_cluster
{
    uint32_t cluster_id;
    char cluster_name[32];
    bool is_client;
    subscribed_attribute_t *attributes;
    struct subscribed_cluster *next;
} subscribed_cluster_t;

// Обновленная структура списка подписки
typedef struct local_device_subscribe_list
{
    uint64_t node_id;
    uint16_t endpoint_id;
    char endpoint_name[32];
    uint32_t device_type_id;
    char device_name[64];

    subscribed_cluster_t *server_clusters;
    subscribed_cluster_t *client_clusters;

    //    bool is_subscribed;
    //    bool is_searched;
    //    void *subscribe_ptr;
    struct local_device_subscribe_list *next;
} local_device_subscribe_list_t;

using namespace esp_matter;
using namespace esp_matter::controller;
using namespace chip;
using namespace chip::app::Clusters;

static void _subscribe_local_device_state(intptr_t arg);
static uint32_t cluster_id = 0x6;
static uint32_t attribute_id = 0x0;
static uint32_t min_interval = 0;
static uint32_t max_interval = 60;
static SemaphoreHandle_t device_list_mutex = NULL;
static local_device_subscribe_list_t *local_subscribe_list = NULL;
static const char *TAG = "app_matter_ctrl";
static matter_device_t *m_device_ptr = NULL;
device_to_control_t device_to_control = {0, 0, NULL};
// extern TaskHandle_t xRefresh_Ui_Handle;

class device_list_lock
{
public:
    device_list_lock()
    {
        if (device_list_mutex)
        {
            xSemaphoreTakeRecursive(device_list_mutex, portMAX_DELAY);
        }
    }

    ~device_list_lock()
    {
        if (device_list_mutex)
        {
            xSemaphoreGiveRecursive(device_list_mutex);
        }
    }
};
/*
static node_endpoint_id_list_t *get_tail_device(node_endpoint_id_list_t *dev_list)
{
    if (!dev_list)
        return NULL;
    node_endpoint_id_list_t *tail = dev_list;
    while (tail->next)
    {
        tail = tail->next;
    }
    return tail;
}
*/

/* be called when subscribe connecting failed */
static void subscribe_failed_cb(void *subscribe_cmd)
{
    //    ESP_LOGI(TAG, "subscribe connecting failed callback");
    esp_matter::controller::subscribe_command *sub_cmd = (subscribe_command *)subscribe_cmd;
    device_list_lock my_device_lock;

    /* the subscribe command will be removed automatically after connecting failed, set the attr in local device
       subscribe list to null */
    local_device_subscribe_list_t *sub_ptr = local_subscribe_list;
    while (sub_ptr)
    {
        subscribed_cluster_t *cluster = sub_ptr->server_clusters;
        while (cluster)
        {
            subscribed_attribute_t *attr = cluster->attributes;
            while (attr)
            {
                if (attr->subscribe_ptr == sub_cmd)
                {
                    ESP_LOGW(TAG, "Subscribe failed for Node: %" PRIu64 ", Endpoint: %u, Cluster (%s): 0x%" PRIx32 ", Attribute (%s): 0x%" PRIx32,
                             sub_ptr->node_id, sub_ptr->endpoint_id, ClusterIdToText(cluster->cluster_id), cluster->cluster_id,
                             AttributeIdToText(cluster->cluster_id, attr->attribute_id));
                    attr->subscribe_ptr = NULL;
                    attr->is_subscribed = false;
                }
                attr = attr->next;
            }
            cluster = cluster->next;
        }
        sub_ptr = sub_ptr->next;
    }
}
/* be called when subscribe timeout */
static void subscribe_done_cb(uint64_t remote_node_id, uint32_t subscription_id)
{
   // ESP_LOGI(TAG, "Subscribe done callback for node %llx, subscription_id %u", remote_node_id, subscription_id);
    device_list_lock my_device_lock;
    local_device_subscribe_list_t *sub_ptr = local_subscribe_list;
    while (sub_ptr)
    {
        if (sub_ptr->node_id == remote_node_id)
        {
            subscribed_cluster_t *cluster = sub_ptr->server_clusters;
            while (cluster)
            {
                subscribed_attribute_t *attr = cluster->attributes;
                while (attr)
                {
                    if (attr->subscribe_ptr && ((subscribe_command *)attr->subscribe_ptr)->get_subscription_id() == subscription_id)
                    {
                        attr->is_subscribed = true;
                        ESP_LOGI(TAG, "Subscribe done for Node: %" PRIu64 ", Endpoint: %u, Cluster (%s): 0x%" PRIx32 ", Attribute (%s): 0x%" PRIx32,
                                 sub_ptr->node_id, sub_ptr->endpoint_id, ClusterIdToText(cluster->cluster_id), cluster->cluster_id,
                                 AttributeIdToText(cluster->cluster_id, attr->attribute_id));
                    }
                    attr = attr->next;
                }
                cluster = cluster->next;
            }
        }
        sub_ptr = sub_ptr->next;
    }
}

static void _subscribe_local_device_state(intptr_t context)
{
    subscribed_attribute_t *attr = (subscribed_attribute_t *)context;
    if (attr && attr->subscribe_ptr)
    {
        subscribe_command *cmd = (subscribe_command *)attr->subscribe_ptr;
        cmd->send_command();
    }
}

void matter_ctrl_subscribe_device_state(subscribe_device_type_t sub_type)
{
    if (SUBSCRIBE_LOCAL_DEVICE == sub_type)
    {
        //    ESP_LOGI(TAG, "Subscribe_device_state");
        device_list_lock my_device_lock;
        local_device_subscribe_list_t *sub_ptr = local_subscribe_list;

        while (sub_ptr)
        {
            subscribed_cluster_t *cluster = sub_ptr->server_clusters;
            while (cluster)
            {
                subscribed_attribute_t *attr = cluster->attributes;
                while (attr)
                {
                    if (!attr->is_subscribed && attr->subscribe)
                    {
                        attr->subscribe_ptr = chip::Platform::New<subscribe_command>(
                            sub_ptr->node_id,
                            sub_ptr->endpoint_id,
                            cluster->cluster_id,
                            attr->attribute_id,
                            SUBSCRIBE_ATTRIBUTE,
                            min_interval,
                            max_interval,
                            true,
                            OnAttributeData,
                            nullptr,
                            subscribe_done_cb,
                            subscribe_failed_cb);

                        if (!attr->subscribe_ptr)
                        {
                            ESP_LOGE(TAG, "Failed to alloc memory for subscribe command");
                            return;
                        }

                        attr->is_subscribed = true;

                        ESP_LOGI(TAG, "Send subscribe request to Node: %" PRIu64 ", Endpoint: %u, Cluster (%s): 0x%" PRIx32 ", Attribute (%s): 0x%" PRIx32,
                                 sub_ptr->node_id, sub_ptr->endpoint_id, ClusterIdToText(cluster->cluster_id), cluster->cluster_id,
                                 AttributeIdToText(cluster->cluster_id, attr->attribute_id));

                        chip::DeviceLayer::PlatformMgr().ScheduleWork(_subscribe_local_device_state, (intptr_t)attr);
                    }
                    attr = attr->next;
                }
                cluster = cluster->next;
            }
            sub_ptr = sub_ptr->next;
        }
    }
}

/*
static void send_command_cb(intptr_t arg)
{
    device_list_lock my_device_lock;
    node_endpoint_id_list_t *ptr = (node_endpoint_id_list_t *)arg;
    if (ptr)
    {
        ESP_LOGI(TAG, "send command to node %llx endpoint %d", ptr->node_id, ptr->endpoint_id);
        esp_matter::controller::send_invoke_cluster_command(ptr->node_id, ptr->endpoint_id, OnOff::Id, OnOff::Commands::Toggle::Id, NULL);
    }
    else
        ESP_LOGE(TAG, "send command with null ptr");
}

void matter_ctrl_change_state(intptr_t arg)
{
    device_list_lock my_device_lock;
    node_endpoint_id_list_t *ptr = (node_endpoint_id_list_t *)arg;
    if (NULL == ptr)
    {
        return;
    }

    bool find_ptr = false;
    node_endpoint_id_list_t *dev_ptr = device_to_control.dev_list;
    // make sure that ptr is in the device_list //
    while (dev_ptr)
    {
        if (dev_ptr == ptr)
        {
            find_ptr = true;
            break;
        }
        dev_ptr = dev_ptr->next;
    }
    if (!find_ptr)
    {
        ESP_LOGE(TAG, "device ptr has been already modified");
        return;
    }

    if (ptr->device_type == CONTROL_LIGHT_DEVICE || ptr->device_type == CONTROL_PLUG_DEVICE)
    {
        // for light and plug matter device, on/off server is supported //
        ptr->OnOff = !ptr->OnOff;
        // ui_set_onoff_state(ptr->lv_obj, ptr->device_type, ptr->OnOff);
        chip::DeviceLayer::PlatformMgr().ScheduleWork(send_command_cb, arg);
    }
}

void matter_device_list_lock()
{
    if (device_list_mutex)
    {
        xSemaphoreTakeRecursive(device_list_mutex, portMAX_DELAY);
    }
}

void matter_device_list_unlock()
{
    if (device_list_mutex)
    {
        xSemaphoreGiveRecursive(device_list_mutex);
    }
}

void matter_ctrl_obj_clear()
{
    device_list_lock my_device_lock;
    node_endpoint_id_list_t *ptr = device_to_control.dev_list;
    while (ptr)
    {
        // ptr->lv_obj = NULL;
        ptr = ptr->next;
    }
}

void matter_factory_reset()
{
    ESP_LOGI(TAG, "Starting factory reset");
    esp_matter::factory_reset();
}
*/


// Освобождение списка атрибутов
static void free_attribute_list(subscribed_attribute_t *attr)
{
    while (attr)
    {
        subscribed_attribute_t *next = attr->next;
        free(attr);
        attr = next;
    }
}
// Освобождение списка кластеров
static void free_cluster_list(subscribed_cluster_t *cluster)
{
    while (cluster)
    {
        subscribed_cluster_t *next = cluster->next;
        free_attribute_list(cluster->attributes);
        free(cluster);
        cluster = next;
    }
}
// Обработка атрибутов кластера
static subscribed_attribute_t *process_attributes(matter_attribute_t *attrs, uint16_t count)
{
    if (!attrs || count == 0)
        return NULL;

    subscribed_attribute_t *head = NULL;
    subscribed_attribute_t *tail = NULL;

    for (size_t i = 0; i < count; i++)
    {
        matter_attribute_t *src_attr = &attrs[i];

        // Создаем новый атрибут для подписки
        subscribed_attribute_t *new_attr = (subscribed_attribute_t *)calloc(1, sizeof(subscribed_attribute_t));
        if (!new_attr)
        {
            ESP_LOGE(TAG, "Failed to allocate attribute entry");
            free_attribute_list(head);
            return NULL;
        }

        // Заполняем данные атрибута
        new_attr->attribute_id = src_attr->attribute_id;
        strncpy(new_attr->attribute_name, src_attr->attribute_name, sizeof(new_attr->attribute_name) - 1);
        new_attr->subscribe = src_attr->subscribe;
        new_attr->is_subscribed = false;
        new_attr->subscribe_ptr = NULL;
        new_attr->next = NULL;

        // Копируем значение атрибута
        memcpy(&new_attr->current_value, &src_attr->current_value, sizeof(esp_matter_attr_val_t));

        // Добавляем в список
        if (!head)
        {
            head = tail = new_attr;
        }
        else
        {
            tail->next = new_attr;
            tail = new_attr;
        }
    }

    return head;
}

// Обработка кластеров устройства
static subscribed_cluster_t *process_clusters(matter_cluster_t *clusters, uint16_t count)
{
    if (!clusters || count == 0)
        return NULL;

    subscribed_cluster_t *head = NULL;
    subscribed_cluster_t *tail = NULL;

    for (size_t i = 0; i < count; i++)
    {
        matter_cluster_t *src_cluster = &clusters[i];

        // Создаем новый кластер для подписки
        subscribed_cluster_t *new_cluster = (subscribed_cluster_t *)calloc(1, sizeof(subscribed_cluster_t));
        if (!new_cluster)
        {
            ESP_LOGE(TAG, "Failed to allocate cluster entry");
            free_cluster_list(head);
            return NULL;
        }

        // Заполняем данные кластера
        new_cluster->cluster_id = src_cluster->cluster_id;
        strncpy(new_cluster->cluster_name, src_cluster->cluster_name, sizeof(new_cluster->cluster_name) - 1);
        new_cluster->is_client = src_cluster->is_client;
        new_cluster->next = NULL;

        // Обрабатываем атрибуты кластера
        new_cluster->attributes = process_attributes(src_cluster->attributes, src_cluster->attributes_count);
        if (!new_cluster->attributes && src_cluster->attributes_count > 0)
        {
            free(new_cluster);
            free_cluster_list(head);
            return NULL;
        }

        // Добавляем в список
        if (!head)
        {
            head = tail = new_cluster;
        }
        else
        {
            tail->next = new_cluster;
            tail = new_cluster;
        }
    }

    return head;
}

esp_err_t matter_ctrl_get_device(void *dev_list)
{
    if (!dev_list)
    {
        ESP_LOGE(TAG, "Invalid device list pointer");
        return ESP_ERR_INVALID_ARG;
    }

    matter_device_t *dev = (matter_device_t *)dev_list;
    device_list_lock my_device_lock; // Автоматическая блокировка/разблокировка

    // Проходим по всем устройствам в списке
    while (dev)
    {
        ESP_LOGI(TAG, "Processing device: %s (NodeID: 0x%016llX)", dev->model_name, dev->node_id);

        // Обрабатываем все endpoint'ы устройства
        for (size_t ep_idx = 0; ep_idx < dev->endpoints_count; ep_idx++)
        {
            endpoint_entry_t *endpoint = &dev->endpoints[ep_idx];

            // Создаем новую запись подписки
            local_device_subscribe_list_t *new_sub = (local_device_subscribe_list_t *)calloc(1, sizeof(local_device_subscribe_list_t));
            if (!new_sub)
            {
                ESP_LOGE(TAG, "Failed to allocate subscription entry");
                return ESP_ERR_NO_MEM;
            }

            // Заполняем базовую информацию
            new_sub->node_id = dev->node_id;
            new_sub->endpoint_id = endpoint->endpoint_id;
            // new_sub->is_subscribed = false;
            // new_sub->subscribe_ptr = NULL;
            new_sub->next = NULL;

            // Обрабатываем серверные кластеры
            new_sub->server_clusters = process_clusters(dev->server_clusters, dev->server_clusters_count);

            // Обрабатываем клиентские кластеры
            new_sub->client_clusters = process_clusters(dev->client_clusters, dev->client_clusters_count);

            // Добавляем в список подписок
            new_sub->next = local_subscribe_list;
            local_subscribe_list = new_sub;

            ESP_LOGI(TAG, "Added subscription for endpoint %d (%s)",
                     endpoint->endpoint_id, endpoint->endpoint_name);
        }

        dev = dev->next;
    }
    return ESP_OK;
}
