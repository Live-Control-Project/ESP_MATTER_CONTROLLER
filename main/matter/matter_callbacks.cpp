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
#include "EntryToText.h"

#include <queue>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <string>
#include <cstring>
#include <cstdio>
#include <inttypes.h>
#include <platform/PlatformManager.h>

static const char *TAG = "AttributeCallback";

extern matter_controller_t g_controller;

// Helper function declarations
static esp_err_t readClustersForEndpoint(uint64_t node_id, uint16_t endpoint, const char *cluster_type);
static void handleEPlist(uint64_t node_id, const chip::app::ConcreteDataAttributePath &path, chip::TLV::TLVReader *data);
static void handleClusterList(uint64_t node_id, const chip::app::ConcreteDataAttributePath &path,
                              chip::TLV::TLVReader *data, matter_controller_t *controller);
static esp_err_t readAttributesForCluster(uint64_t node_id, uint16_t endpoint_id,
                                          uint32_t cluster_id, matter_controller_t *controller);
static esp_err_t readBasicInformation(uint64_t node_id);

// Constants
static constexpr uint32_t DESCRIPTOR_CLUSTER_ID = 0x001D;
static constexpr uint32_t BASIC_CLUSTER_ID = 0x0028;

static std::unordered_map<uint64_t, std::unordered_set<uint16_t>> processed_endpoints;

// schedule_controller_request_attribute(node_id, endpoint_id, cluster_id, attribute_id, command_type);
void schedule_controller_request_attribute(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_or_event_id, esp_matter::controller::read_command_type_t command_type)
{
    // Копируем параметры в heap, чтобы они были доступны внутри лямбды
    auto *params = new std::tuple<uint64_t, uint16_t, uint32_t, uint32_t, esp_matter::controller::read_command_type_t>(
        node_id, endpoint_id, cluster_id, attribute_or_event_id, command_type);

    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t arg)
        {
            auto *params = reinterpret_cast<std::tuple<uint64_t, uint16_t, uint32_t, uint32_t, esp_matter::controller::read_command_type_t> *>(arg);
            esp_matter::command::controller_request_attribute(
                std::get<0>(*params),
                std::get<1>(*params),
                std::get<2>(*params),
                std::get<3>(*params),
                std::get<4>(*params));
            delete params;
        },
        reinterpret_cast<intptr_t>(params));
}

// ---------------- очередь запросов на чтение атрибутов ----------------
/*
struct AttributeRequest
{
    uint64_t node_id;
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    esp_matter::controller::read_command_type_t type;
};

static std::queue<AttributeRequest> attribute_request_queue;
static std::mutex attribute_queue_mutex;
static bool attribute_request_in_progress = false;

//

// Функция для добавления запроса в очередь
void enqueue_attribute_request(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter::controller::read_command_type_t type)
{
    std::lock_guard<std::mutex> lock(attribute_queue_mutex);
    attribute_request_queue.push({node_id, endpoint_id, cluster_id, attribute_id, type});
    if (!attribute_request_in_progress)
    {
        attribute_request_in_progress = true;
        // Запускаем первый запрос
        auto req = attribute_request_queue.front();
        esp_matter::command::controller_request_attribute(req.node_id, req.endpoint_id, req.cluster_id, req.attribute_id, req.type);
    }
}

// Вызов из OnReadDone
extern "C" void OnReadDone(
    uint64_t node_id,
    const chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> &attr_paths,
    const chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> &event_paths)
{
    for (size_t i = 0; i < attr_paths.AllocatedSize(); ++i)
    {
        const auto &path = attr_paths[i];
        ESP_LOGI(TAG, "✅  ReadDone Attribute: endpoint=0x%04x, cluster=0x%08" PRIx32 ", attribute=0x%08" PRIx32,
                 path.mEndpointId, path.mClusterId, path.mAttributeId);
    }

    for (size_t i = 0; i < event_paths.AllocatedSize(); ++i)
    {
        const auto &path = event_paths[i];
        ESP_LOGI(TAG, "✅  ReadDone Event: endpoint=0x%04x, cluster=0x%08" PRIx32 ", event=0x%08" PRIx32,
                 path.mEndpointId, path.mClusterId, path.mEventId);
    }

    // После обработки текущего запроса — отправляем следующий из очереди
    std::lock_guard<std::mutex> lock(attribute_queue_mutex);
    if (!attribute_request_queue.empty())
    {
        attribute_request_queue.pop();
    }
    if (!attribute_request_queue.empty())
    {
        auto req = attribute_request_queue.front();
        esp_matter::command::controller_request_attribute(req.node_id, req.endpoint_id, req.cluster_id, req.attribute_id, req.type);
        ESP_LOGI(TAG, "⏩  Next attribute request: Node: %" PRIu64 ", Endpoint: %u, Cluster: 0x%" PRIx32 ", Attribute: 0x%" PRIx32,
                 req.node_id, req.endpoint_id, req.cluster_id, req.attribute_id);
    }
    else
    {
        attribute_request_in_progress = false;
    }
}
    */
// ---------------- очередь запросов на чтение атрибутов ----------------

//-------------------------------------------------------------------------

// Вызов из OnReadDone
extern "C" void OnReadDone(
    uint64_t node_id,
    const chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> &attr_paths,
    const chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> &event_paths)
{
    for (size_t i = 0; i < attr_paths.AllocatedSize(); ++i)
    {
        const auto &path = attr_paths[i];
        ESP_LOGI(TAG, "readDone Attribute: endpoint=0x%04x, cluster=0x%08" PRIx32 ", attribute=0x%08" PRIx32,
                 path.mEndpointId, path.mClusterId, path.mAttributeId);
    }
}
// Основной обработчик атрибутов
void OnAttributeData(uint64_t node_id,
                     const chip::app::ConcreteDataAttributePath &path,
                     chip::TLV::TLVReader *data)
{

    ESP_LOGI(TAG, "⏪ Attribute report from Node: %" PRIu64 ", Endpoint: %u, Cluster (%s): 0x%" PRIx32 ", Attribute (%s): 0x%" PRIx32,
             node_id, path.mEndpointId,
             ClusterIdToText(path.mClusterId) ? ClusterIdToText(path.mClusterId) : "Unknown",
             path.mClusterId,
             AttributeIdToText(path.mClusterId, path.mAttributeId) ? AttributeIdToText(path.mClusterId, path.mAttributeId) : "Unknown",
             path.mAttributeId);

    if (!data)
    {
        ESP_LOGW(TAG, "TLVReader is null");
        return;
    }

    // Handle parts list (descriptor cluster)
    if (path.mClusterId == DESCRIPTOR_CLUSTER_ID && path.mAttributeId == 0x0003)
    {
        handleEPlist(node_id, path, data);
        return;
    }

    // Handle cluster list (descriptor cluster) - только один раз на endpoint
    if (path.mClusterId == DESCRIPTOR_CLUSTER_ID &&
        processed_endpoints[node_id].find(path.mEndpointId) == processed_endpoints[node_id].end())
    {
        processed_endpoints[node_id].insert(path.mEndpointId);
        handleClusterList(node_id, path, data, &g_controller);
        return;
    }
    matter_device_t *node = find_node(&g_controller, node_id);
    if (!node)
    {
        ESP_LOGE(TAG, "Node %" PRIu64 " not found", node_id);
        return;
    }

    if (path.mClusterId == BASIC_CLUSTER_ID)
    {
        switch (path.mAttributeId)
        {
        case 0x0001: // VendorName
        case 0x0003: // ProductName
        case 0x0005: // NodeLabel
        case 0x000a: // FirmwareVersion
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;
                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    const char *field_name = "";
                    char *target = nullptr;
                    size_t target_size = 0;

                    switch (path.mAttributeId)
                    {
                    case 0x0001:
                        field_name = "VendorName";
                        target = node->vendor_name;
                        target_size = sizeof(node->vendor_name);
                        break;
                    case 0x0003:
                        field_name = "ProductName";
                        target = node->model_name;
                        target_size = sizeof(node->model_name);
                        break;
                    case 0x0005:
                        field_name = "NodeLabel";
                        target = node->description;
                        target_size = sizeof(node->description);
                        break;
                    case 0x000a:
                        field_name = "FirmwareVersion";
                        target = node->firmware_version;
                        target_size = sizeof(node->firmware_version);
                        break;
                    }

                    ESP_LOGI(TAG, "%s: %.*s", field_name, static_cast<int>(value.size()), value.data());
                    if (target && target_size > 0)
                    {
                        size_t copy_len = value.size() < (target_size - 1) ? value.size() : (target_size - 1);
                        memcpy(target, value.data(), copy_len);
                        target[copy_len] = '\0';
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Target buffer for %s is NULL or size is zero!", field_name);
                    }

                    if (path.mAttributeId == 0x000a)
                    {
                        log_controller_structure(&g_controller);

                        /*
                        esp_err_t ret = subscribe_all_marked_attributes(&g_controller);
                        if (ret != ESP_OK)
                        {
                            ESP_LOGE(TAG, "Failed to subscribe to all marked attributes: %s", esp_err_to_name(ret));
                        }
                        */
                    }
                }
            }
            break;
        }

        case 0x0002: // VendorID
        case 0x0004: // ProductID
        {
            if (data->GetType() == chip::TLV::kTLVType_UTF8String)
            {
                chip::CharSpan value;
                if (data->Get(value) == CHIP_NO_ERROR)
                {
                    std::string str_value(value.data(), value.size());
                    const char *field_name = path.mAttributeId == 0x0002 ? "VendorID" : "ProductID";
                    ESP_LOGI(TAG, "%s: %.*s", field_name, static_cast<int>(value.size()), value.data());

                    if (path.mAttributeId == 0x0002)
                    {
                        node->vendor_id = static_cast<uint32_t>(strtoul(str_value.c_str(), nullptr, 10));
                    }
                    else
                    {
                        node->product_id = static_cast<uint16_t>(strtoul(str_value.c_str(), nullptr, 10));
                    }
                }
            }
            else if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
            {
                uint64_t num_value = 0;
                if (data->Get(num_value) == CHIP_NO_ERROR)
                {
                    if (path.mAttributeId == 0x0002)
                    {
                        node->vendor_id = static_cast<uint32_t>(num_value);
                        ESP_LOGI(TAG, "VendorID (numeric): %" PRIu32, node->vendor_id);
                    }
                    else
                    {
                        node->product_id = static_cast<uint16_t>(num_value);
                        ESP_LOGI(TAG, "ProductID (numeric): %" PRIu16, node->product_id);
                    }
                }
            }
            break;
        }

        default:
            ESP_LOGW(TAG, "Unhandled attribute ID: 0x%04X", path.mAttributeId);
            break;
        }
        return;
    }

    // Handle attribute values
    esp_matter_attr_val_t attr_val = {};
    CHIP_ERROR err = CHIP_NO_ERROR;

    switch (data->GetType())
    {
    case chip::TLV::kTLVType_SignedInteger:
    {
        int64_t value = 0;
        if ((err = data->Get(value)) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Signed): %lld", value);
            attr_val.type = ESP_MATTER_VAL_TYPE_INT32;
            attr_val.val.i32 = static_cast<int32_t>(value);
        }
        break;
    }
    case chip::TLV::kTLVType_UnsignedInteger:
    {
        uint64_t value = 0;
        if ((err = data->Get(value)) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Unsigned): %llu", value);
            attr_val.type = ESP_MATTER_VAL_TYPE_UINT32;
            attr_val.val.u32 = static_cast<uint32_t>(value);
        }
        break;
    }
    case chip::TLV::kTLVType_Boolean:
    {
        bool value = false;
        if ((err = data->Get(value)) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Boolean): %s", value ? "true" : "false");
            attr_val.type = ESP_MATTER_VAL_TYPE_BOOLEAN;
            attr_val.val.b = value;
        }
        break;
    }
    case chip::TLV::kTLVType_FloatingPointNumber:
    {
        double value = 0.0;
        if ((err = data->Get(value)) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (Float): %f", value);
            attr_val.type = ESP_MATTER_VAL_TYPE_FLOAT;
            attr_val.val.f = static_cast<float>(value);
        }
        break;
    }
    case chip::TLV::kTLVType_UTF8String:
    {
        chip::CharSpan value;
        if ((err = data->Get(value)) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (String): %.*s", static_cast<int>(value.size()), value.data());
            attr_val.type = ESP_MATTER_VAL_TYPE_CHAR_STRING;
            attr_val.val.a.b = reinterpret_cast<uint8_t *>(const_cast<char *>(value.data()));
            attr_val.val.a.s = value.size();
            attr_val.val.a.n = value.size();
            attr_val.val.a.t = value.size();
        }
        break;
    }
    case chip::TLV::kTLVType_ByteString:
    {
        chip::ByteSpan value;
        if ((err = data->Get(value)) == CHIP_NO_ERROR)
        {
            ESP_LOGI(TAG, "  Value (ByteString, len=%u)", static_cast<unsigned>(value.size()));
            attr_val.type = ESP_MATTER_VAL_TYPE_OCTET_STRING;
            attr_val.val.a.b = const_cast<uint8_t *>(value.data());
            attr_val.val.a.s = value.size();
            attr_val.val.a.n = value.size();
            attr_val.val.a.t = value.size();
        }
        break;
    }
    case chip::TLV::kTLVType_Null:
    {
        ESP_LOGI(TAG, "  Value: NULL");
        attr_val.type = ESP_MATTER_VAL_TYPE_INVALID;
        break;
    }
    case chip::TLV::kTLVType_Structure:
    case chip::TLV::kTLVType_Array:
    case chip::TLV::kTLVType_List:
    {
        ESP_LOGI(TAG, "  Container type: %d", data->GetType());
        chip::TLV::TLVType containerType;
        if (data->EnterContainer(containerType) == CHIP_NO_ERROR)
        {
            while (data->Next() == CHIP_NO_ERROR)
            {
                // Process nested TLV data if needed
            }
            data->ExitContainer(containerType);
        }
        return;
    }
    default:
        ESP_LOGI(TAG, "  Unhandled TLV type: %d", data->GetType());
        return;
    }

    if (err == CHIP_NO_ERROR)
    {
        // Сначала проверяем wildcard
        // Сначала проверяем wildcard
        if (path.mAttributeId == 0xFFFFFFFF)
        {
            ESP_LOGW(TAG, "Skip handle_attribute_report for wildcard attribute");
            return;
        }

        // Защита: если строка пуста, не передавать nullptr
        if ((attr_val.type == ESP_MATTER_VAL_TYPE_CHAR_STRING || attr_val.type == ESP_MATTER_VAL_TYPE_OCTET_STRING) &&
            (attr_val.val.a.b == nullptr || attr_val.val.a.s == 0))
        {
            ESP_LOGW(TAG, "Skip handle_attribute_report: empty string or null pointer");
            return;
        }

        handle_attribute_report(&g_controller, node_id, path.mEndpointId,
                                path.mClusterId, path.mAttributeId, &attr_val);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get TLV value: %s", chip::ErrorStr(err));
    }
}

static esp_err_t readBasicInformation(uint64_t node_id)
{
    const char *attributes[] = {"0x0001", "0x0002", "0x0003", "0x0004", "0x0005", "0x000a"};
    const size_t attr_count = sizeof(attributes) / sizeof(attributes[0]);
    char node_str[21];

    snprintf(node_str, sizeof(node_str), "%" PRIu64, node_id);
    ESP_LOGI(TAG, "Reading %zu basic attributes from node %" PRIu64, attr_count, node_id);

    for (size_t i = 0; i < attr_count; i++)
    {
        ESP_LOGI(TAG, "Reading attribute %s (%zu/%zu)", attributes[i], i + 1, attr_count);
        uint32_t attr_id = strtoul(attributes[i], nullptr, 0);
        // enqueue_attribute_request(node_id, 0x0000, BASIC_CLUSTER_ID, attr_id, esp_matter::controller::READ_ATTRIBUTE);
        // esp_matter::command::controller_request_attribute(node_id, 0x0000, BASIC_CLUSTER_ID, attr_id, esp_matter::controller::READ_ATTRIBUTE);
        schedule_controller_request_attribute(node_id, 0x0000, BASIC_CLUSTER_ID, attr_id, esp_matter::controller::READ_ATTRIBUTE);
        }
    return ESP_OK;
}

static void handleEPlist(uint64_t node_id,
                         const chip::app::ConcreteDataAttributePath &path,
                         chip::TLV::TLVReader *data)
{
    ESP_LOGI(TAG, "Endpoint %u: Descriptor->PartsList (endpoint's list)", path.mEndpointId);
    if (!data)
    {
        ESP_LOGE(TAG, "TLVReader is null");
        return;
    }

    chip::TLV::TLVType outerType;
    if (data->EnterContainer(outerType) != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container");
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

                // Read Server clusters
                if (readClustersForEndpoint(node_id, endpoint, "0x0001") != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to read server clusters for endpoint %u", endpoint);
                }

                // Read Client clusters
                if (readClustersForEndpoint(node_id, endpoint, "0x0002") != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to read client clusters for endpoint %u", endpoint);
                }
            }
        }
    }

    data->ExitContainer(outerType);
}

static esp_err_t readClustersForEndpoint(uint64_t node_id, uint16_t endpoint, const char *cluster_type)
{
    char node_str[21];
    char endpoint_str[6];

    snprintf(node_str, sizeof(node_str), "%" PRIu64, node_id);
    snprintf(endpoint_str, sizeof(endpoint_str), "%u", endpoint);

    uint32_t cluster_id = strtoul(cluster_type, nullptr, 0);
    // enqueue_attribute_request(node_id, endpoint, DESCRIPTOR_CLUSTER_ID, cluster_id, esp_matter::controller::READ_ATTRIBUTE);
    // esp_matter::command::controller_request_attribute(node_id, endpoint, DESCRIPTOR_CLUSTER_ID, cluster_id, esp_matter::controller::READ_ATTRIBUTE);
    schedule_controller_request_attribute(node_id, endpoint, DESCRIPTOR_CLUSTER_ID, cluster_id, esp_matter::controller::READ_ATTRIBUTE);

    ESP_LOGI(TAG, "Reading %s clusters for endpoint %u on node %" PRIu64, cluster_type, endpoint, node_id);
    return ESP_OK;
}

// другой вариант. Не используется
static void read_all_clusters_for_endpoint(uint64_t node_id, uint16_t endpoint_id, matter_controller_t *controller)
{
    matter_device_t *node = find_node(controller, node_id);
    if (!node)
    {
        ESP_LOGE(TAG, "Node with ID %" PRIu64 " not found", node_id);
        return;
    }

    for (int i = 0; i < node->endpoints_count; ++i)
    {
        endpoint_entry_t *ep = &node->endpoints[i];
        if (ep->endpoint_id != endpoint_id)
            continue;
        ESP_LOGI(TAG, "Reading clusters for endpoint %u on node %" PRIu64, endpoint_id, node_id);
        if (ep->cluster_count == 0)
        {
            ESP_LOGW(TAG, "No clusters found for endpoint %u on node %" PRIu64, endpoint_id, node_id);
            continue;
        }

        for (int j = 0; j < ep->cluster_count; ++j)
        {
            uint32_t cluster_id = ep->clusters[j];
            if (endpoint_id > 0)
            {
                ESP_LOGI(TAG, "Reading attributes for cluster 0x%04X on endpoint %u", cluster_id, endpoint_id);
                readAttributesForCluster(node_id, endpoint_id, cluster_id, controller);
            }
        }
    }
}

static esp_err_t readAttributesForCluster(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, matter_controller_t *controller)
{
    if (!controller)
    {
        ESP_LOGE(TAG, "Controller is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    // enqueue_attribute_request(node_id, endpoint_id, cluster_id, 0xFFFFFFFF, esp_matter::controller::READ_ATTRIBUTE);
    // esp_matter::command::controller_request_attribute(node_id, endpoint_id, cluster_id, 0xFFFFFFFF, esp_matter::controller::READ_ATTRIBUTE);
    schedule_controller_request_attribute(node_id, endpoint_id, cluster_id, 0xFFFFFFFF, esp_matter::controller::READ_ATTRIBUTE);

    ESP_LOGI(TAG, "Reading all attributes for cluster 0x%04X on endpoint %u of node %" PRIu64,
             cluster_id, endpoint_id, node_id);
    return ESP_OK;
}

static void handleClusterList(uint64_t node_id,
                              const chip::app::ConcreteDataAttributePath &path,
                              chip::TLV::TLVReader *data,
                              matter_controller_t *controller)
{
    ESP_LOGI(TAG, "Endpoint %u: Descriptor->ClusterList", path.mEndpointId);
    if (!data)
    {
        ESP_LOGE(TAG, "TLVReader is null");
        return;
    }
    chip::TLV::TLVType outerType;
    if (data->EnterContainer(outerType) != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to enter TLV container");
        return;
    }

    std::vector<uint32_t> clusters;
    while (data->Next() == CHIP_NO_ERROR)
    {
        if (data->GetType() == chip::TLV::kTLVType_UnsignedInteger)
        {
            uint32_t cluster_id = 0;
            if (data->Get(cluster_id) == CHIP_NO_ERROR)
            {
                if (std::find(clusters.begin(), clusters.end(), cluster_id) == clusters.end())
                {

                    handle_attribute_report(&g_controller, node_id, path.mEndpointId, cluster_id, 0x9999, nullptr, false);

                    if (path.mEndpointId > 0 && cluster_id != DESCRIPTOR_CLUSTER_ID && cluster_id != BASIC_CLUSTER_ID && cluster_id != 0x0003 && cluster_id != 0x0004 && cluster_id != 0x0062)
                    {
                        ESP_LOGI(TAG, "Reading attributes for cluster 0x%04X (%s) on endpoint %u ", cluster_id, ClusterIdToText(cluster_id), path.mEndpointId);
                        readAttributesForCluster(node_id, path.mEndpointId, cluster_id, controller);
                    }
                    clusters.push_back(cluster_id);
                }
            }
        }
    }
    data->ExitContainer(outerType);

    matter_device_t *node = find_node(controller, node_id);
    if (!node)
    {
        ESP_LOGE(TAG, "Node %" PRIu64 " not found", node_id);
        return;
    }
    if (node)
    {
        for (int i = 0; i < node->endpoints_count; ++i)
        {
            endpoint_entry_t *ep = &node->endpoints[i];
            if (ep->endpoint_id == path.mEndpointId)
            {
                ep->cluster_count = std::min((int)clusters.size(), (int)(sizeof(ep->clusters) / sizeof(ep->clusters[0])));
                for (int j = 0; j < ep->cluster_count; ++j)
                {
                    ep->clusters[j] = clusters[j];
                }
                break;
            }
        }
        if (readBasicInformation(node_id) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read Basic Information for node %" PRIu64, node_id);
        }
    }
}
