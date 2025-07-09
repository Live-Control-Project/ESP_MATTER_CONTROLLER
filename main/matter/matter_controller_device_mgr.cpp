// Copyright 2023 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <esp_check.h>
#include <esp_matter_controller_utils.h>
#include <matter_controller_cluster.h>
#include <lib/support/ScopedBuffer.h>
#include "matter_controller_device_mgr.h"

using chip::Platform::ScopedMemoryBufferWithSize;
using namespace esp_matter::cluster::matter_controller::attribute;

#define TAG "controller_dev_mgr"

extern matter_controller_t g_controller;

namespace esp_matter
{
  namespace controller
  {
    namespace device_mgr
    {

      static matter_device_t *s_matter_device_list = NULL;
      static device_list_update_callback_t s_device_list_update_cb = NULL;
      static QueueHandle_t s_task_queue = NULL;
      static TaskHandle_t s_device_mgr_task = NULL;
      static SemaphoreHandle_t s_device_mgr_mutex = NULL;

      typedef esp_err_t (*esp_matter_device_mgr_task_t)(void *);

      typedef struct
      {
        esp_matter_device_mgr_task_t task;
        void *arg;
      } task_post_t;

      class scoped_device_mgr_lock
      {
      public:
        scoped_device_mgr_lock()
        {
          if (s_device_mgr_mutex)
          {
            xSemaphoreTake(s_device_mgr_mutex, portMAX_DELAY);
          }
          else
          {
            ESP_LOGE(TAG, "device mgr lock not initialized");
          }
        }
        ~scoped_device_mgr_lock()
        {
          if (s_device_mgr_mutex)
          {
            xSemaphoreGive(s_device_mgr_mutex);
          }
          else
          {
            ESP_LOGE(TAG, "device mgr lock not initialized");
          }
        }
      };

      template <typename T>
      void safe_free(T *&ptr)
      {
        if (ptr)
        {
          free(ptr);
          ptr = nullptr;
        }
      }

      void free_matter_device_list(matter_node *node_list)
      {
        matter_node *current = node_list;
        while (current)
        {
          matter_node *next = current->next;

          safe_free(current->endpoints);

          if (current->server_clusters)
          {
            for (uint16_t i = 0; i < current->server_clusters_count; i++)
            {
              safe_free(current->server_clusters[i].attributes);
            }
            safe_free(current->server_clusters);
            current->server_clusters_count = 0;
          }

          if (current->client_clusters)
          {
            for (uint16_t i = 0; i < current->client_clusters_count; i++)
            {
              safe_free(current->client_clusters[i].attributes);
            }
            safe_free(current->client_clusters);
            current->client_clusters_count = 0;
          }

          safe_free(current);
          current = next;
        }
      }

      matter_node *copy_device_list(const matter_node *src_list)
      {
        if (!src_list)
          return nullptr;

        matter_node *new_list = nullptr;
        matter_node *tail = nullptr;
        const matter_node *current = src_list;

        while (current)
        {
          matter_node *new_node = (matter_node *)calloc(1, sizeof(matter_node));
          if (!new_node)
          {
            free_matter_device_list(new_list);
            return nullptr;
          }

          *new_node = *current;
          new_node->next = nullptr;
          new_node->endpoints = nullptr;
          new_node->server_clusters = nullptr;
          new_node->client_clusters = nullptr;

          if (current->endpoints && current->endpoints_count > 0)
          {
            new_node->endpoints = (endpoint_entry_t *)calloc(current->endpoints_count, sizeof(endpoint_entry_t));
            if (!new_node->endpoints)
            {
              free(new_node);
              free_matter_device_list(new_list);
              return nullptr;
            }
            memcpy(new_node->endpoints, current->endpoints, current->endpoints_count * sizeof(endpoint_entry_t));
          }

          if (current->server_clusters && current->server_clusters_count > 0)
          {
            new_node->server_clusters = (matter_cluster_t *)calloc(current->server_clusters_count, sizeof(matter_cluster_t));
            if (!new_node->server_clusters)
            {
              free_matter_device_list(new_node);
              free_matter_device_list(new_list);
              return nullptr;
            }
            for (uint16_t i = 0; i < current->server_clusters_count; i++)
            {
              new_node->server_clusters[i] = current->server_clusters[i];
              new_node->server_clusters[i].attributes = nullptr;

              if (current->server_clusters[i].attributes && current->server_clusters[i].attributes_count > 0)
              {
                new_node->server_clusters[i].attributes = (matter_attribute_t *)calloc(
                    current->server_clusters[i].attributes_count, sizeof(matter_attribute_t));
                if (!new_node->server_clusters[i].attributes)
                {
                  free_matter_device_list(new_node);
                  free_matter_device_list(new_list);
                  return nullptr;
                }
                memcpy(new_node->server_clusters[i].attributes,
                       current->server_clusters[i].attributes,
                       current->server_clusters[i].attributes_count * sizeof(matter_attribute_t));
              }
            }
          }

          if (current->client_clusters && current->client_clusters_count > 0)
          {
            new_node->client_clusters = (matter_cluster_t *)calloc(current->client_clusters_count, sizeof(matter_cluster_t));
            if (!new_node->client_clusters)
            {
              free_matter_device_list(new_node);
              free_matter_device_list(new_list);
              return nullptr;
            }
            for (uint16_t i = 0; i < current->client_clusters_count; i++)
            {
              new_node->client_clusters[i] = current->client_clusters[i];
              new_node->client_clusters[i].attributes = nullptr;

              if (current->client_clusters[i].attributes && current->client_clusters[i].attributes_count > 0)
              {
                new_node->client_clusters[i].attributes = (matter_attribute_t *)calloc(
                    current->client_clusters[i].attributes_count, sizeof(matter_attribute_t));
                if (!new_node->client_clusters[i].attributes)
                {
                  free_matter_device_list(new_node);
                  free_matter_device_list(new_list);
                  return nullptr;
                }
                memcpy(new_node->client_clusters[i].attributes,
                       current->client_clusters[i].attributes,
                       current->client_clusters[i].attributes_count * sizeof(matter_attribute_t));
              }
            }
          }

          if (!new_list)
          {
            new_list = new_node;
            tail = new_node;
          }
          else
          {
            tail->next = new_node;
            tail = new_node;
          }

          current = current->next;
        }

        return new_list;
      }

      matter_device_t *clone_device(const matter_device_t *src)
      {
        if (!src)
          return nullptr;

        matter_device_t *dst = (matter_device_t *)calloc(1, sizeof(matter_device_t));
        if (!dst)
          return nullptr;

        *dst = *src;
        dst->next = nullptr;

        if (src->endpoints && src->endpoints_count > 0)
        {
          dst->endpoints = (endpoint_entry_t *)calloc(src->endpoints_count, sizeof(endpoint_entry_t));
          if (!dst->endpoints)
          {
            free(dst);
            return nullptr;
          }
          memcpy(dst->endpoints, src->endpoints, src->endpoints_count * sizeof(endpoint_entry_t));
        }

        if (src->server_clusters && src->server_clusters_count > 0)
        {
          dst->server_clusters = (matter_cluster_t *)calloc(
              src->server_clusters_count, sizeof(matter_cluster_t));
          if (!dst->server_clusters)
          {
            free_matter_device_list(dst);
            return nullptr;
          }
          for (uint16_t i = 0; i < src->server_clusters_count; i++)
          {
            dst->server_clusters[i] = src->server_clusters[i];
            dst->server_clusters[i].attributes = nullptr;

            if (src->server_clusters[i].attributes && src->server_clusters[i].attributes_count > 0)
            {
              dst->server_clusters[i].attributes = (matter_attribute_t *)calloc(
                  src->server_clusters[i].attributes_count, sizeof(matter_attribute_t));
              if (!dst->server_clusters[i].attributes)
              {
                free_matter_device_list(dst);
                return nullptr;
              }
              memcpy(dst->server_clusters[i].attributes,
                     src->server_clusters[i].attributes,
                     src->server_clusters[i].attributes_count * sizeof(matter_attribute_t));
            }
          }
        }

        if (src->client_clusters && src->client_clusters_count > 0)
        {
          dst->client_clusters = (matter_cluster_t *)calloc(
              src->client_clusters_count, sizeof(matter_cluster_t));
          if (!dst->client_clusters)
          {
            free_matter_device_list(dst);
            return nullptr;
          }
          for (uint16_t i = 0; i < src->client_clusters_count; i++)
          {
            dst->client_clusters[i] = src->client_clusters[i];
            dst->client_clusters[i].attributes = nullptr;

            if (src->client_clusters[i].attributes && src->client_clusters[i].attributes_count > 0)
            {
              dst->client_clusters[i].attributes = (matter_attribute_t *)calloc(
                  src->client_clusters[i].attributes_count, sizeof(matter_attribute_t));
              if (!dst->client_clusters[i].attributes)
              {
                free_matter_device_list(dst);
                return nullptr;
              }
              memcpy(dst->client_clusters[i].attributes,
                     src->client_clusters[i].attributes,
                     src->client_clusters[i].attributes_count * sizeof(matter_attribute_t));
            }
          }
        }

        return dst;
      }

      static esp_err_t update_device_list_task(void *endpoint_id_ptr)
      {
        if (!endpoint_id_ptr)
        {
          ESP_LOGE(TAG, "endpoint_id_ptr is NULL");
          return ESP_ERR_INVALID_ARG;
        }

                if (esp_get_minimum_free_heap_size() < 1024 * 10)
        {
          ESP_LOGW(TAG, "Low memory, skipping update");
          free(endpoint_id_ptr);
          vTaskDelay(pdMS_TO_TICKS(1000));
          return ESP_OK;
        }

        uint16_t endpoint_id = *(uint16_t *)endpoint_id_ptr;
        free(endpoint_id_ptr);

        scoped_device_mgr_lock lock;

        if (!g_controller.nodes_list)
        {
          ESP_LOGW(TAG, "No devices found in controller list");
          // free_matter_device_list((matter_node *)s_matter_device_list);
          // s_matter_device_list = nullptr;

          if (s_device_list_update_cb)
          {
            s_device_list_update_cb();
          }
          return ESP_OK;
        }

        matter_node *new_list = copy_device_list(g_controller.nodes_list);
        if (!new_list)
        {
          ESP_LOGE(TAG, "Failed to copy device list");
          return ESP_ERR_NO_MEM;
        }

        free_matter_device_list((matter_node *)s_matter_device_list);
        s_matter_device_list = new_list;

        if (s_device_list_update_cb)
        {
          s_device_list_update_cb();
        }

        return ESP_OK;
      }

      matter_device_t *get_device_list_clone()
      {
        matter_device_t *ret = NULL;
        matter_device_t *current = s_matter_device_list;
        while (current)
        {
          matter_device_t *tmp = clone_device(current);
          if (!tmp)
          {
            free_matter_device_list((matter_node *)ret);
            return NULL;
          }
          tmp->next = ret;
          ret = tmp;
          current = current->next;
        }
        return ret;
      }

      static matter_device_t *get_device(uint64_t node_id)
      {
        matter_device_t *ret = s_matter_device_list;
        while (ret)
        {
          if (ret->node_id == node_id)
          {
            break;
          }
          ret = ret->next;
        }
        return ret;
      }

      matter_device_t *get_device_clone(uint64_t node_id)
      {
        return clone_device(get_device(node_id));
      }

      void set_device_reachable(uint64_t node_id, bool value)
      {
        matter_device_t *ptr = g_controller.nodes_list;
        while (ptr)
        {
          if (ptr->node_id == node_id)
          {
            ptr->reachable = value;
            break;
          }
          ptr = ptr->next;
        }
      }

      esp_err_t update_device_list(uint16_t endpoint_id)
      {
        if (!s_task_queue)
        {
          ESP_LOGE(TAG, "Failed to update device list as the task queue is not initialized");
          return ESP_ERR_INVALID_STATE;
        }

        uint16_t *endpoint_id_ptr = (uint16_t *)malloc(sizeof(uint16_t));
        if (!endpoint_id_ptr)
        {
          return ESP_ERR_NO_MEM;
        }
        *endpoint_id_ptr = endpoint_id;
        task_post_t task_post = {
            .task = update_device_list_task,
            .arg = endpoint_id_ptr,
        };
        if (xQueueSend(s_task_queue, &task_post, portMAX_DELAY) != pdTRUE)
        {
          free(endpoint_id_ptr);
          ESP_LOGE(TAG, "Failed send update device list task");
          return ESP_FAIL;
        }
        return ESP_OK;
      }

      static void device_mgr_task(void *aContext)
      {
        s_task_queue = xQueueCreate(8 /* Queue Size */, sizeof(task_post_t));
        if (!s_task_queue)
        {
          ESP_LOGE(TAG, "Failed to create device mgr task queue");
          return;
        }
        s_device_mgr_mutex = xSemaphoreCreateRecursiveMutex();
        if (!s_device_mgr_mutex)
        {
          ESP_LOGE(TAG, "Failed to create device mgr lock");
          vQueueDelete(s_task_queue);
          return;
        }

        update_device_list(0);

        task_post_t task_post;
        while (true)
        {
          if (xQueueReceive(s_task_queue, &task_post, portMAX_DELAY) == pdTRUE)
          {
            task_post.task(task_post.arg);
          }
        }
        vQueueDelete(s_task_queue);
        vSemaphoreDelete(s_device_mgr_mutex);
        vTaskDelete(NULL);
      }

      esp_err_t init(uint16_t endpoint_id,
                     device_list_update_callback_t dev_list_update_cb)
      {
        if (s_device_mgr_task)
        {
          return ESP_OK;
        }

        s_device_list_update_cb = dev_list_update_cb;

        if (xTaskCreate(device_mgr_task, "device_mgr", 4096, NULL, 5,
                        &s_device_mgr_task) != pdTRUE)
        {
          ESP_LOGE(TAG, "Failed to create device mgr task");
          return ESP_ERR_NO_MEM;
        }

        return ESP_OK;
      }

    } // namespace device_mgr
  } // namespace controller
} // namespace esp_matter