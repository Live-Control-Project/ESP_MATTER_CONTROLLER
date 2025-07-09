/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <app/server/Server.h>
#include <app_priv.h>
#include <device.h>
#include <esp_check.h>
#include <esp_matter.h>
#include <esp_matter_controller_cluster_command.h>
#include <led_driver.h>
#include <matter_controller_device_mgr.h>
#include <system/SystemClock.h>
#include <system/SystemLayerImplFreeRTOS.h>

#include "app_matter_ctrl.h"
#include "nvs_flash.h"

using namespace esp_matter;
using namespace chip::app::Clusters;
extern matter_controller_t g_controller;

static const char *TAG = "app_driver";
static const uint16_t DEVICE_UPDATE_TIMER_SEC = 40;
static uint64_t device_node_id = 0;
static matter_device_t *s_device_ptr = NULL;
// static TaskHandle_t xRefresh_Ui_Handle = NULL;
static bool device_get_flag = false;

static int count_devices(const matter_device_t *device_list)
{
    int count = 0;
    const matter_device_t *current = device_list;
    while (current)
    {
        count++;
        current = current->next;
    }
    return count;
}
/* Callback after updating device list */
void on_device_list_update(void)
{
    static uint32_t last_update = 0;
    uint32_t now = esp_log_timestamp();

    // Защита от слишком частых обновлений
    if (last_update != 0 && (now - last_update) < 5000)
    {
        ESP_LOGD(TAG, "Skipping too frequent update");
        return;
    }
    last_update = now;

    matter_device_t *new_list = esp_matter::controller::device_mgr::get_device_list_clone();
    if (!new_list)
    {
        ESP_LOGE(TAG, "Failed to get device list clone");
        return;
    }

    // Атомарная замена списка устройств
    matter_device_t *old_list = s_device_ptr;
    s_device_ptr = new_list;

    // Проверка изменений
    bool list_changed = !old_list || !new_list || (old_list->node_id != new_list->node_id);

    if (list_changed)
    {
        ESP_LOGI(TAG, "Device list updated, count: %d", count_devices(new_list));
        matter_ctrl_get_device((void *)s_device_ptr);
        matter_ctrl_subscribe_device_state(SUBSCRIBE_LOCAL_DEVICE);
    }

    if (old_list)
    {
        esp_matter::controller::device_mgr::free_matter_device_list(old_list);
    }
}

app_driver_handle_t app_driver_button_init(void *user_data)
{
    /* Initialize button */
    button_config_t config = button_driver_get_config();
    button_handle_t handle = iot_button_create(&config);
    return (app_driver_handle_t)handle;
}

static void refresh_ui_task(void *pvParameters)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        /* Refresh UI here */
        // clean_screen_with_button();
        // ui_matter_config_update_cb(UI_MATTER_EVT_REFRESH);
    }
}

static void Layer_timer_cb(chip::System::Layer *aLayer, void *appState)
{
    //    if (!device_get_flag)
    //    {
    //        ESP_LOGE(TAG, "Timer callback skipped because device_get_flag=false");
    //        return;
    //    }

    // Логирование состояния памяти
    nvs_stats_t nvs_stats;
    nvs_get_stats("nvs", &nvs_stats);
    ESP_LOGI("NVS", "Used entries: %d, Free entries: %d", nvs_stats.used_entries, nvs_stats.free_entries);
    ESP_LOGI("HEAP", "Free heap: %u Kb", esp_get_free_heap_size() / 1024);
    ESP_LOGI("HEAP", "Min free heap: %u Kb", esp_get_minimum_free_heap_size() / 1024);

    // Обновление списка устройств
    esp_err_t err = esp_matter::controller::device_mgr::update_device_list(0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to update device list: %d", err);
    }

    matter_ctrl_subscribe_device_state(SUBSCRIBE_LOCAL_DEVICE);

    // Перезапускаем таймер
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    CHIP_ERROR chip_err = chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Seconds32(DEVICE_UPDATE_TIMER_SEC), Layer_timer_cb, nullptr);
    // ESP_LOGI(TAG, "Timer start result: %d", chip_err);
    esp_matter::lock::chip_stack_unlock();

    if (chip_err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to restart update timer");
    }
}

esp_err_t update_device_init()
{
    // Инициализация таймера обновления
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    CHIP_ERROR chip_err = chip::DeviceLayer::SystemLayer().StartTimer(
        chip::System::Clock::Seconds32(DEVICE_UPDATE_TIMER_SEC), Layer_timer_cb, nullptr);
    // ESP_LOGI(TAG, "Timer start result: %d", chip_err);
    esp_matter::lock::chip_stack_unlock();

    if (chip_err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to start update timer");
        return ESP_FAIL;
    }

    // Создаем задачу для обновления UI
    /*
    if (xTaskCreatePinnedToCore(refresh_ui_task, "refresh_ui", 4096, nullptr,
                                tskIDLE_PRIORITY, &xRefresh_Ui_Handle, 1) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create refresh UI task");
        return ESP_FAIL;
    }
    */

    return ESP_OK;
}