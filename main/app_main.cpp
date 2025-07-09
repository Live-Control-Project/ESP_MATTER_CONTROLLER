/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

// #include <esp_matter.h>
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
#include "matter_callbacks.h"
#include "devices.h"
// #include "matter_controller_device_mgr.h"

#include <app_priv.h>

static const char *TAG = "app_main";
uint16_t switch_endpoint_id = 0;
extern bool device_get_flag;

static uint16_t app_endpoint_id;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;

matter_controller_t g_controller = {0};
static bool attributes_subscribed = false;

void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:

        if (event->InterfaceIpAddressChanged.Type == chip::DeviceLayer::InterfaceIpChangeType::kIpV4_Assigned)
        {
            ESP_LOGI(TAG, "Interface IP Address changed");
            esp_matter::controller::device_mgr::init(0, on_device_list_update);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInternetConnectivityChange:
        ESP_LOGI(TAG, "Internet connectivity change");
        if ((event->InternetConnectivityChange.IPv4 == chip::DeviceLayer::kConnectivity_Established ||
             event->InternetConnectivityChange.IPv6 == chip::DeviceLayer::kConnectivity_Established) &&
            !attributes_subscribed)
        {
            // subscribe_all_marked_attributes(&g_controller);
            // attributes_subscribed = true;
            // ESP_LOGI(TAG, "Subscribed to all marked attributes. InternetConnectivityChange");
        }
        break;
        /*
            case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kInterfaceIpAddressChanged:
                ESP_LOGI(TAG, "Interface IP Address changed");
                if (!attributes_subscribed)
                {
                    subscribe_all_marked_attributes(&g_controller);
                    attributes_subscribed = true;
                    ESP_LOGI(TAG, "Subscribed to all marked attributes. InterfaceIpAddressChanged");
                }
                break;
                */
    case chip::DeviceLayer::DeviceEventType::PublicEventTypes::kThreadConnectivityChange:
        ESP_LOGI(TAG, "Thread connectivity change");
        if (event->ThreadConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established &&
            !attributes_subscribed)
        {
            //    subscribe_all_marked_attributes(&g_controller);
            //    attributes_subscribed = true;
            //    ESP_LOGI(TAG, "Subscribed to all marked attributes after Thread join");
        }
        break;

        // Установлена безопасная сессия с устройством
    case chip::DeviceLayer::DeviceEventType::kSecureSessionEstablished:
    {
        uint64_t peer_node_id = event->SecureSessionEstablished.PeerNodeId;
        ESP_LOGI("AppEvent", "Безопасная сессия установлена с Node ID: 0x%" PRIx64, peer_node_id);

        // !!! ToDo переделать механизм подписки. Сделать флаг, что подписка выполнена успешно. Подписываться на конкретную ноду
        // subscribe_all_marked_attributes(&g_controller);
        // attributes_subscribed = true;
        // ESP_LOGI(TAG, "Subscribed to all marked attributes after Thread join");

        break;
    }

    // Устройство стало операционным (например, подключилось к Thread/WiFi)
    case chip::DeviceLayer::DeviceEventType::kOperationalNetworkEnabled:
    {
        ESP_LOGI("AppEvent", "Операционная сеть активирована");
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kESPSystemEvent:
        if (event->Platform.ESPSystemEvent.Base == IP_EVENT &&
            event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP)
        {
#ifdef CONFIG_OPENTHREAD_BORDER_ROUTER
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

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGE(TAG, "Commissioning failed, fail safe timer expired");
        if (!device_get_flag)
        {
            //  ui_matter_config_update_cb(UI_MATTER_EVT_FAILED_COMMISSION);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        if (!device_get_flag)
        {
            // ui_matter_config_update_cb(UI_MATTER_EVT_START_COMMISSION);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kWiFiConnectivityChange:
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "kFabricCommitted");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    default:
        break;
    }
}

/*
esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                  uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (!val)
    {
        ESP_LOGE(TAG, "Invalid attribute value pointer");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Attribute update - endpoint: 0x%04X, cluster: 0x%08lX, attribute: 0x%08lX",
             endpoint_id, cluster_id, attribute_id);
    return ESP_OK;
}
esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %d, effect: %d", type, effect_id);
    return ESP_OK;
}
*/
extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    esp_err_t ret = nvs_flash_init();
    //-----------------------------------------------------------------------------//
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

    // Загружаем из NVS список устройств
    matter_controller_init(&g_controller, 0x123, 1);

    /* Create a Matter node */
    /*
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    // The node and endpoint handles can be used to create/add other endpoints and clusters.
    if (!node)
    {
        ESP_LOGE(TAG, "Matter node creation failed");
    }

    // Add custom matter controller cluster
    node_t *matter_node = node::get();
    if (!matter_node)
    {
        ESP_LOGE(TAG, "Matter node creation failed");
        abort();
    }
    endpoint_t *root_endpoint = esp_matter::endpoint::get(matter_node, 0);

    // app_matter_endpoint_create();
*/

//-----------------------------------------------------------------------------//
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
#ifdef CONFIG_AUTO_UPDATE_RCP
    esp_vfs_spiffs_conf_t rcp_fw_conf = {
        .base_path = "/rcp_fw", .partition_label = "rcp_fw", .max_files = 10, .format_if_mount_failed = false};
    if (ESP_OK != esp_vfs_spiffs_register(&rcp_fw_conf))
    {
        ESP_LOGE(TAG, "Failed to mount rcp firmware storage");
        return;
    }
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
    openthread_init_br_rcp(&rcp_update_config);
#endif
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
    update_device_init();
}
