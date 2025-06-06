#include "matter_callbacks.h"
#include "esp_log.h"
#include "mqtt.h"
#include "settings.h"
#include "cJSON.h"
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <lib/core/TLVReader.h>
#include "devices.h"

static const char *TAG = "AttributeCallback";

void OnAttributeData(uint64_t node_id,
                     const chip::app::ConcreteDataAttributePath &path,
                     chip::TLV::TLVReader *data)
{
    matter_controller_t controller = {0};
    matter_controller_init(&controller, 0x123, 1);

    ESP_LOGI(TAG, "Attribute report from node %llu", node_id);
    ESP_LOGI(TAG, "Endpoint: %u, Cluster: 0x%" PRIx32 ", Attribute: 0x%" PRIx32,
             path.mEndpointId, path.mClusterId, path.mAttributeId);

    char topic[128];
    if (path.mEndpointId > 1)
    {
        snprintf(topic, sizeof(topic), "%s/fd/matter/%llu/%u", sys_settings.mqtt.prefix, node_id, path.mEndpointId);
    }
    else
    {
        snprintf(topic, sizeof(topic), "%s/fd/matter/%llu", sys_settings.mqtt.prefix, node_id);
    }

    if (path.mClusterId == chip::app::Clusters::OnOff::Id)
    {
        if (path.mAttributeId == chip::app::Clusters::OnOff::Attributes::OnOff::Id)
        {
            bool onOff;
            CHIP_ERROR err = data->Get(onOff);
            if (err == CHIP_NO_ERROR)
            {
                ESP_LOGI(TAG, "OnOff state: %s", onOff ? "ON" : "OFF");

                // Отправка состояния через MQTT
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "status", onOff ? "on" : "off");

                char *json_str = cJSON_PrintUnformatted(root);
                if (json_str)
                {
                    esp_err_t ret = mqtt_publish_data(topic, json_str);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "MQTT publish failed with error: %s", esp_err_to_name(ret));
                    }
                    free(json_str);
                }
                cJSON_Delete(root);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read OnOff value: %s", chip::ErrorStr(err));
            }
        }
    }
    //------------------------------------------------------------------------
    // Чтение данных о подключенном устройстве
    ESP_LOGI(TAG, "Read attribute callback: node=0x%llx endpoint=%u cluster=0x%lx attr=0x%lx",
             node_id, path.mEndpointId, path.mClusterId, path.mAttributeId);

    if (!data)
    {
        ESP_LOGW(TAG, "TLVReader is null");
        return;
    }

    chip::TLV::TLVType outerType;
    CHIP_ERROR err = data->EnterContainer(outerType);
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container: %s", chip::ErrorStr(err));
        return;
    }

    while (true)
    {
        chip::TLV::Tag tag;
        chip::TLV::TLVType type;
        err = data->Next();
        if (err == CHIP_END_OF_TLV)
        {
            break;
        }
        if (err != CHIP_NO_ERROR)
        {
            ESP_LOGE(TAG, "TLV Next() error: %s", chip::ErrorStr(err));
            break;
        }

        // Для массивов DeviceTypeList, ServerList, ClientList, PartsList элементы — UINT или STRUCT
        if (data->GetType() == chip::TLV::kTLVType_Structure)
        {
            // Например, DeviceTypeList — массив структур
            chip::TLV::TLVType structType;
            err = data->EnterContainer(structType);
            if (err == CHIP_NO_ERROR)
            {
                uint32_t deviceType = 0;
                uint16_t revision = 0;
                while (data->Next() == CHIP_NO_ERROR)
                {
                    if (data->GetTag() == chip::TLV::ContextTag(0))
                    {
                        data->Get(deviceType);
                    }
                    else if (data->GetTag() == chip::TLV::ContextTag(1))
                    {
                        data->Get(revision);
                    }
                }
                ESP_LOGI(TAG, "  DeviceType: 0x%lx, Revision: %u", deviceType, revision);
                data->ExitContainer(structType);
            }
        }
        else if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
        {
            uint32_t value = 0;
            data->Get(value);
            ESP_LOGI(TAG, "  Value: 0x%lx", value);

            handle_attribute_report(&controller, node_id, path.mEndpointId, value, 0x0, NULL);
        }
    }
    log_controller_structure(&controller);
    data->ExitContainer(outerType);

    //------------------------------------------------------------------------
}
