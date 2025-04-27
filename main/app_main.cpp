/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_console.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_ota.h>
#if CONFIG_OPENTHREAD_BORDER_ROUTER
#include <esp_openthread_border_router.h>
#include <esp_openthread_lock.h>
#include <esp_ot_config.h>
#include <esp_spiffs.h>
#include <platform/ESP32/OpenthreadLauncher.h>
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER
#include <app_reset.h>
#include <common_macros.h>

#include <app/server/Server.h>
#include <credentials/FabricTable.h>

#include "wifi/settings.h"
#include "wifi/bus.h"
#include "wifi/wifi.h"

#include "console/console.h"

static const char *TAG = "app_main";
uint16_t switch_endpoint_id = 0;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;
    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
            event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP)
        {
#if CONFIG_OPENTHREAD_BORDER_ROUTER
            static bool sThreadBRInitialized = false;
            if (!sThreadBRInitialized)
            {
                esp_openthread_set_backbone_netif(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
                esp_openthread_lock_acquire(portMAX_DELAY);
                esp_openthread_border_router_init();
                esp_openthread_lock_release();
                sThreadBRInitialized = true;
            }
#endif
        }
        break;
    default:
        break;
    }
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // Загружаем настройки (если их нет — установятся значения по умолчанию)
    if (settings_load_from_nvs() != ESP_OK)
    {
        ESP_LOGW("SETTINGS", "No saved settings, using defaults");
    }

    console_init();

    // TODO: Настройки WiFi пока прибиты гвоздями
    // sys_settings.wifi.mode = WIFI_MODE_STA;
    // memcpy(sys_settings.wifi.sta.ssid, "Mikro", strlen("Mikro") + 1);
    // memcpy(sys_settings.wifi.sta.password, "4455667788", strlen("4455667788") + 1);

    // memcpy(sys_settings.mqtt.server, "mqtt://live-control.com:1883", strlen("mqtt://live-control.com:1883") + 1);
    // memcpy(sys_settings.mqtt.server, "mqtt://192.168.0.100:1883", strlen("mqtt://192.168.0.100:1883") + 1);

    // memcpy(sys_settings.mqtt.user, "guest", strlen("guest") + 1);
    // memcpy(sys_settings.mqtt.password, "guest", strlen("guest") + 1);

    // Сохраняем с проверкой ошибок
    /*
    ret = settings_save_to_nvs();
    if (ret != ESP_OK)
    {
        ESP_LOGE("SETTINGS", "Save failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI("SETTINGS", "Settings saved successfully!");
    }
        */
    // Инициализация шины
    ret = bus_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE("MAIN", "Bus init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Инициализация Wi-Fi
    if (strncmp((const char *)sys_settings.wifi.sta.ssid, "MyWiFi", sizeof(sys_settings.wifi.sta.ssid)) != 0)
    {
        ret = wifi_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE("MAIN", "Wi-Fi init failed: %s", esp_err_to_name(ret));
            return;
        }
    }

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::init();
#if CONFIG_ESP_MATTER_CONTROLLER_ENABLE
    esp_matter::console::controller_register_commands();
#endif // CONFIG_ESP_MATTER_CONTROLLER_ENABLE
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
    esp_matter::console::otcli_register_commands();
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER
#endif // CONFIG_ENABLE_CHIP_SHELL
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif // CONFIG_OPENTHREAD_BORDER_ROUTER
    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
    esp_matter::lock::chip_stack_lock(portMAX_DELAY);
    esp_matter::controller::matter_controller_client::get_instance().init(112233, 1, 5580);
    esp_matter::controller::matter_controller_client::get_instance().setup_commissioner();
    esp_matter::lock::chip_stack_unlock();
#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
}
