#include "matter_callbacks.h"
#include "esp_log.h"
#include "mqtt.h"
#include "settings.h"
#include "cJSON.h"
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <lib/core/TLVReader.h>
#include "devices.h"
#include "matter_command.h"

static const char *TAG = "AttributeCallback";

// Объявление вспомогательных функций
static esp_err_t readClustersForEndpoint(uint64_t node_id, uint16_t endpoint, const char *cluster_type);
static void handlePartsList(uint64_t node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data);
static void handleClusterList(uint64_t node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data, matter_controller_t *controller);

void OnAttributeData(uint64_t node_id,
                     const chip::app::ConcreteDataAttributePath &path,
                     chip::TLV::TLVReader *data)
{
    matter_controller_t controller = {0};
    matter_controller_init(&controller, 0x123, 1);

    ESP_LOGI(TAG, "Attribute report from node %llu", node_id);
    ESP_LOGI(TAG, "Endpoint: %u, Cluster: 0x%" PRIx32 ", Attribute: 0x%" PRIx32,
             path.mEndpointId, path.mClusterId, path.mAttributeId);

    if (!data)
    {
        ESP_LOGW(TAG, "TLVReader is null");
        return;
    }

    // Handle special cases first
    if (path.mClusterId == 0x001D && path.mAttributeId == 0x0003)
    {
        handlePartsList(node_id, path, data);
        return;
    }

    if (path.mClusterId == 0x001D && path.mAttributeId == 0x0001)
    {
        handleClusterList(node_id, path, data, &controller);
        return;
    }

    if (data->GetType() == chip::TLV::kTLVType_SignedInteger)
    {
        int64_t value = 0;
        if (data->Get(value) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Signed): %lld", value);
            esp_matter_attr_val_t attr_val = {};
            attr_val.type = ESP_MATTER_VAL_TYPE_INT32;
            attr_val.val.i32 = (int32_t)value;
            handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
        }
    }
    else if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
    {
        uint64_t value = 0;
        if (data->Get(value) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Unsigned): %llu", value);
            esp_matter_attr_val_t attr_val = {};
            attr_val.type = ESP_MATTER_VAL_TYPE_UINT32;
            attr_val.val.u32 = (uint32_t)value;
            handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
        }
    }
    else if (data->GetType() == chip::TLV::kTLVType_Boolean)
    {
        bool value = false;
        if (data->Get(value) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Boolean): %s", value ? "true" : "false");
            esp_matter_attr_val_t attr_val = {};
            attr_val.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
            attr_val.val.b = value;
            handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
        }
    }
    else if (data->GetType() == chip::TLV::kTLVType_FloatingPointNumber)
    {
        double value = 0.0;
        if (data->Get(value) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Float): %f", value);
            esp_matter_attr_val_t attr_val = {};
            attr_val.type = ESP_MATTER_VAL_TYPE_FLOAT;
            attr_val.val.f = (float)value;
            handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
        }
    }
    else if (data->GetType() == chip::TLV::kTLVType_UTF8String)
    {
        chip::CharSpan value;
        if (data->Get(value) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (String): %.*s", static_cast<int>(value.size()), value.data());
            esp_matter_attr_val_t attr_val = {};
            attr_val.type = ESP_MATTER_VAL_TYPE_CHAR_STRING;
            attr_val.val.a.b = (uint8_t *)value.data();
            attr_val.val.a.s = value.size();
            attr_val.val.a.n = value.size();
            attr_val.val.a.t = value.size();
            handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
        }
    }
    else if (data->GetType() == chip::TLV::kTLVType_ByteString)
    {
        chip::ByteSpan value;
        if (data->Get(value) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (ByteString, len=%u)", (unsigned)value.size());
            esp_matter_attr_val_t attr_val = {};
            attr_val.type = ESP_MATTER_VAL_TYPE_OCTET_STRING;
            attr_val.val.a.b = (uint8_t *)value.data();
            attr_val.val.a.s = value.size();
            attr_val.val.a.n = value.size();
            attr_val.val.a.t = value.size();
            handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
        }
    }
    else if (data->GetType() == chip::TLV::kTLVType_Null)
    {
        ESP_LOGI(TAG, "  Value: NULL");
        esp_matter_attr_val_t attr_val = {};
        attr_val.type = ESP_MATTER_VAL_TYPE_INVALID;
        handle_attribute_report(&controller, node_id, path.mEndpointId, path.mClusterId, path.mAttributeId, &attr_val);
    }
    else if (data->GetType() == chip::TLV::kTLVType_Structure ||
             data->GetType() == chip::TLV::kTLVType_Array ||
             data->GetType() == chip::TLV::kTLVType_List)
    {
        ESP_LOGI(TAG, "  Container type: %d", data->GetType());
        chip::TLV::TLVType containerType;
        if (data->EnterContainer(containerType) == CHIP_NO_ERROR)
        {
            // Рекурсивно обработать содержимое контейнера
            while (data->Next() == CHIP_NO_ERROR)
            {
                // Рекурсивно вызвать этот же блок для вложенных TLV
                // (можно вынести в отдельную функцию для чистоты)
            }
            data->ExitContainer(containerType);
        }
    }
    else
    {
        ESP_LOGI(TAG, "  Unhandled TLV type: %d", data->GetType());
    }
}

static void handlePartsList(uint64_t node_id,
                            const chip::app::ConcreteDataAttributePath &path,
                            chip::TLV::TLVReader *data)
{
    ESP_LOGI(TAG, "Endpoint %u: Descriptor->PartsList (endpoint's list):", path.mEndpointId);

    chip::TLV::TLVType outerType;
    CHIP_ERROR err = data->EnterContainer(outerType);
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container: %s", chip::ErrorStr(err));
        return;
    }

    int idx = 0;
    while (data->Next() == CHIP_NO_ERROR)
    {
        if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
        {
            uint16_t endpoint = 0;
            if (data->Get(endpoint) == CHIP_NO_ERROR)
            {
                ESP_LOGI(TAG, "  [%d] Endpoint ID: %u", ++idx, endpoint);

                // Чтение Server кластеров
                esp_err_t ret = readClustersForEndpoint(node_id, endpoint, "0x0001");
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to read server clusters for endpoint %u: %s",
                             endpoint, esp_err_to_name(ret));
                }

                // Чтение Client кластеров
                ret = readClustersForEndpoint(node_id, endpoint, "0x0002");
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to read client clusters for endpoint %u: %s",
                             endpoint, esp_err_to_name(ret));
                }
            }
        }
    }
    data->ExitContainer(outerType);
}

static esp_err_t readClustersForEndpoint(uint64_t node_id, uint16_t endpoint, const char *cluster_type)
{

    char node_str[21];    // достаточно для uint64_t
    char endpoint_str[6]; // достаточно для uint16_t
    snprintf(node_str, sizeof(node_str), "%llu", node_id);
    snprintf(endpoint_str, sizeof(endpoint_str), "%u", endpoint);

    char *argv[] = {
        node_str,
        endpoint_str,
        const_cast<char *>("0x001D"),
        const_cast<char *>(cluster_type)};

    esp_err_t ret = esp_matter::command::controller_read_attr(4, argv);

    return ret;
}

static void handleClusterList(uint64_t node_id,
                              const chip::app::ConcreteDataAttributePath &path,
                              chip::TLV::TLVReader *data,
                              matter_controller_t *controller)
{
    ESP_LOGI(TAG, "Endpoint %u: Descriptor->ClusterList:", path.mEndpointId);

    chip::TLV::TLVType outerType;
    CHIP_ERROR err = data->EnterContainer(outerType);
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container: %s", chip::ErrorStr(err));
        return;
    }

    int idx = 0;
    while (data->Next() == CHIP_NO_ERROR)
    {
        if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
        {
            uint32_t value = 0;
            if (data->Get(value) == CHIP_NO_ERROR)
            {
                ESP_LOGI(TAG, "  Cluster: 0x%lx", value);
                handle_attribute_report(controller, node_id, path.mEndpointId, value, 0x0, NULL);
            }
        }
    }
    data->ExitContainer(outerType);

    log_controller_structure(controller);
}