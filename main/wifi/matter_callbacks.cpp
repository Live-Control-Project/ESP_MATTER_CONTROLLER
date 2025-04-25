#include "matter_callbacks.h"
#include "esp_log.h"
#include "mqtt.h"
#include "settings.h"
#include "cJSON.h"
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <lib/core/TLVReader.h>

static const char *TAG = "AttributeCallback";

void OnAttributeData(uint64_t node_id,
                     const chip::app::ConcreteDataAttributePath &path,
                     chip::TLV::TLVReader *data)
{
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
}
