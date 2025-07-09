/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <esp_check.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_commissioning_window_opener.h>
#include <esp_matter_controller_group_settings.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_subscribe_command.h>
#include <esp_matter_controller_utils.h>
#include <esp_matter_controller_write_command.h>
#include <lib/core/CHIPCore.h>
#include <lib/shell/Commands.h>
#include <lib/shell/Engine.h>
#include <lib/shell/commands/Help.h>
#include <lib/shell/streamer.h>
#include <lib/support/CHIPArgParser.hpp>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <protocols/secure_channel/RendezvousParameters.h>
#include <protocols/user_directed_commissioning/UserDirectedCommissioning.h>
#include "matter_command.h"
#include "matter_callbacks.h"
#include "mqtt.h"
#include <esp_matter.h>
#include <esp_matter_core.h>
#include <esp_matter_client.h>
#include <freertos/FreeRTOS.h>

#include <memory>
#include <platform/ESP32/OpenthreadLauncher.h>

#include <openthread/dataset.h>
#include <openthread/instance.h>
#include <esp_openthread.h>
#include <esp_err.h>
#include "devicemanager/devices.h"
#include "settings.h"
#include <string>
#include <inttypes.h>

using chip::NodeId;
using chip::Inet::IPAddress;
using chip::Platform::ScopedMemoryBufferWithSize;
using chip::Transport::PeerAddress;
// using namespace esp_matter;
// using namespace esp_matter::controller;
extern matter_controller_t g_controller;
const char *TAG = "MatterComand";

namespace esp_matter
{

    namespace command
    {

        // -------------------- Callbacks for PairingCommand -----------------
        void on_pase_callback(CHIP_ERROR err)
        {
            ESP_LOGI(TAG, "PASE session %s", err == CHIP_NO_ERROR ? "success" : "failed");
        }

        static bool pairing_in_progress = false;

        void on_commissioning_success_callback(chip::ScopedNodeId peer_id)
        {
            // параметры NodeId и FabricIndex перепутаны местами при создании объекта ScopedNodeId
            // Правильный порядок аргументов в esp_matter_controller_pairing_command.cpp:
            // 1. NodeId (должен быть первым)
            // 2. FabricIndex (должен быть вторым)
            // m_callbacks.commissioning_success_callback(
            //    chip::ScopedNodeId(peerId.GetNodeId(), fabric->GetFabricIndex())
            //);
            pairing_in_progress = false;
            uint64_t nodeId = peer_id.GetNodeId();
            uint32_t fabricIndex = peer_id.GetFabricIndex();

            // Log in both hex and decimal formats
            ESP_LOGI(TAG, "Commissioning success with fabricIndex 0x%" PRIX32 " (dec %" PRIu32 ") node 0x%" PRIX64 " (dec %" PRIu64 ")",
                     fabricIndex, fabricIndex, nodeId, nodeId);

            // MQTT notification in format {"device":"40:4c:ca:ff:fe:4e:e9:58","event":"deviceJoined"}
            char json_str[64];
            snprintf(json_str, sizeof(json_str), "{\"device\":\"%" PRIX64 "\",\"status\":\"deviceJoined\"}", nodeId);

            // Form topic
            const char *mqttPrefix = sys_settings.mqtt.prefix;
            const char *envtopic = "/event/matter/";
            char eventTopic[128];
            snprintf(eventTopic, sizeof(eventTopic), "%s%s", mqttPrefix, envtopic);

            mqtt_publish_data(eventTopic, json_str);

            // Read attribute 0x0003 from cluster 0x001D endpoint 0x0000
            int argc = 4;
            char nodeIdStr[32];
            snprintf(nodeIdStr, sizeof(nodeIdStr), "%" PRIu64, nodeId);
            char *argv[] = {nodeIdStr, (char *)"0x0000", (char *)"0x001D", (char *)"0x0003"};
            esp_err_t result = esp_matter::command::controller_read_attr(argc, argv);
            if (result != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read Node structure for node 0x%" PRIX64 ": %s", nodeId, esp_err_to_name(result));
            }
            else
            {
                ESP_LOGI(TAG, "read Node structure for node 0x%" PRIu64, nodeId);
            }
        }

        void on_commissioning_failure_callback(
            chip::ScopedNodeId peer_id, CHIP_ERROR error, chip::Controller::CommissioningStage stage,
            std::optional<chip::Credentials::AttestationVerificationResult> additional_err_info)
        {
            pairing_in_progress = false;
            uint64_t nodeId = peer_id.GetNodeId();
            uint32_t fabricIndex = peer_id.GetFabricIndex();

            ESP_LOGE(TAG, "Commissioning failed for node 0x%" PRIX64 " (fabric 0x%" PRIX32 ") at stage %d: %" CHIP_ERROR_FORMAT,
                     nodeId, fabricIndex, static_cast<int>(stage), error.Format());

            // MQTT уведомление
            const char *mqttPrefix = sys_settings.mqtt.prefix;
            const char *envtopic = "/event/matter/";
            char eventTopic[128];
            snprintf(eventTopic, sizeof(eventTopic), "%s%s", mqttPrefix, envtopic);

            char json_str[128];
            snprintf(json_str, sizeof(json_str),
                     "{\"device\":\"%" PRIX64 "\",\"status\":\"JoinFailed\",\"error\":\"%s\",\"stage\":%d}",
                     nodeId, chip::ErrorStr(error), static_cast<int>(stage));
            mqtt_publish_data(eventTopic, json_str);
        }

        // --------------------Колбэки для PairingCommand----------------

        size_t get_array_size(const char *str)
        {
            if (!str)
            {
                return 0;
            }
            size_t ret = 1;
            for (size_t i = 0; i < strlen(str); ++i)
            {
                if (str[i] == ',')
                {
                    ret++;
                }
            }
            return ret;
        }

        esp_err_t string_to_uint32_array(const char *str, ScopedMemoryBufferWithSize<uint32_t> &uint32_array)
        {
            size_t array_len = get_array_size(str);
            if (array_len == 0)
            {
                return ESP_ERR_INVALID_ARG;
            }
            uint32_array.Calloc(array_len);
            if (!uint32_array.Get())
            {
                return ESP_ERR_NO_MEM;
            }
            char number[11]; // max(strlen("0xFFFFFFFF"), strlen("4294967295")) + 1
            const char *next_number_start = str;
            char *next_number_end = NULL;
            size_t next_number_len = 0;
            for (size_t i = 0; i < array_len; ++i)
            {
                next_number_end = strchr(next_number_start, ',');
                if (next_number_end > next_number_start)
                {
                    next_number_len = std::min((size_t)(next_number_end - next_number_start), sizeof(number) - 1);
                }
                else if (i == array_len - 1)
                {
                    next_number_len = strnlen(next_number_start, sizeof(number) - 1);
                }
                else
                {
                    return ESP_ERR_INVALID_ARG;
                }
                strncpy(number, next_number_start, next_number_len);
                number[next_number_len] = 0;
                uint32_array[i] = string_to_uint32(number);
                if (next_number_end > next_number_start)
                {
                    next_number_start = next_number_end + 1;
                }
            }
            return ESP_OK;
        }

        esp_err_t string_to_uint16_array(const char *str, ScopedMemoryBufferWithSize<uint16_t> &uint16_array)
        {
            size_t array_len = get_array_size(str);
            if (array_len == 0)
            {
                return ESP_ERR_INVALID_ARG;
            }
            uint16_array.Calloc(array_len);
            if (!uint16_array.Get())
            {
                return ESP_ERR_NO_MEM;
            }
            char number[7]; // max(strlen(0xFFFF), strlen(65535)) + 1
            const char *next_number_start = str;
            char *next_number_end = NULL;
            size_t next_number_len = 0;
            for (size_t i = 0; i < array_len; ++i)
            {
                next_number_end = strchr(next_number_start, ',');
                if (next_number_end > next_number_start)
                {
                    next_number_len = std::min((size_t)(next_number_end - next_number_start), sizeof(number) - 1);
                }
                else if (i == array_len - 1)
                {
                    next_number_len = strnlen(next_number_start, sizeof(number) - 1);
                }
                else
                {
                    return ESP_ERR_INVALID_ARG;
                }
                strncpy(number, next_number_start, next_number_len);
                number[next_number_len] = 0;
                uint16_array[i] = string_to_uint16(number);
                if (next_number_end > next_number_start)
                {
                    next_number_start = next_number_end + 1;
                }
            }
            return ESP_OK;
        }

#if defined(CONFIG_ENABLE_ESP32_BLE_CONTROLLER) && defined(CONFIG_ESP_MATTER_COMMISSIONER_ENABLE)
        int char_to_int(char ch)
        {
            if ('A' <= ch && ch <= 'F')
            {
                return 10 + ch - 'A';
            }
            else if ('a' <= ch && ch <= 'f')
            {
                return 10 + ch - 'a';
            }
            else if ('0' <= ch && ch <= '9')
            {
                return ch - '0';
            }
            return -1;
        }

        bool convert_hex_str_to_bytes(const char *hex_str, uint8_t *bytes, uint8_t &bytes_len)
        {
            if (!hex_str)
            {
                return false;
            }
            size_t hex_str_len = strlen(hex_str);
            if (hex_str_len == 0 || hex_str_len % 2 != 0 || hex_str_len / 2 > bytes_len)
            {
                return false;
            }
            bytes_len = hex_str_len / 2;
            for (size_t i = 0; i < bytes_len; ++i)
            {
                int byte_h = char_to_int(hex_str[2 * i]);
                int byte_l = char_to_int(hex_str[2 * i + 1]);
                if (byte_h < 0 || byte_l < 0)
                {
                    return false;
                }
                bytes[i] = (byte_h << 4) + byte_l;
            }
            return true;
        }
#endif // defined(CONFIG_ENABLE_ESP32_BLE_CONTROLLER) && defined(CONFIG_ESP_MATTER_COMMISSIONER_ENABLE)

#if CONFIG_ESP_MATTER_COMMISSIONER_ENABLE
        esp_err_t controller_pairing(int argc, char **argv)
        {
            if (pairing_in_progress)
            {
                ESP_LOGW(TAG, "Pairing already in progress, please wait.");
                return ESP_ERR_INVALID_STATE;
            }
            pairing_in_progress = true;

            VerifyOrReturnError(argc >= 3 && argc <= 5, ESP_ERR_INVALID_ARG);
            esp_err_t result = ESP_ERR_INVALID_ARG;

            esp_matter::controller::pairing_command_callbacks_t cbs = {
                .pase_callback = on_pase_callback,
                .commissioning_success_callback = on_commissioning_success_callback,
                .commissioning_failure_callback = on_commissioning_failure_callback,
            };
            esp_matter::controller::pairing_command::get_instance().set_callbacks(cbs);

            // ищем в matter_controller_t номер последней ноды . Создаем новый номер для новой ноды.
            uint64_t nodeId = 0;

            if (g_controller.nodes_count > 0)
            {
                nodeId = g_controller.nodes_list[g_controller.nodes_count - 1].node_id + 1;
            }
            else
            {
                nodeId = 12344321; // chip::kTestDeviceNodeId; // начнем с тестового ID
            }

            if (strncmp(argv[0], "onnetwork", sizeof("onnetwork")) == 0)
            {
                ESP_LOGI(TAG, "Pairing on network command");
                VerifyOrReturnError(argc == 2, ESP_ERR_INVALID_ARG);
                //                uint64_t nodeId = string_to_uint64(argv[1]);
                uint32_t pincode = string_to_uint32(argv[1]);
                result = controller::pairing_on_network(nodeId, pincode);

#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
            }
            else if (strncmp(argv[0], "ble-wifi", sizeof("ble-wifi")) == 0)
            {

                ESP_LOGI(TAG, "Pairing over BLE and Wi-Fi command");
                VerifyOrReturnError(argc == 5, ESP_ERR_INVALID_ARG);
                // uint64_t nodeId = string_to_uint64(argv[1]);
                uint32_t pincode = string_to_uint32(argv[3]);
                uint16_t disc = string_to_uint16(argv[4]);

                result = controller::pairing_ble_wifi(nodeId, pincode, disc, argv[1], argv[2]);
            }
            else if (strncmp(argv[0], "ble-thread", sizeof("ble-thread")) == 0)
            {
                ESP_LOGI(TAG, "Pairing over BLE and Thread command");
                VerifyOrReturnError(argc >= 3 && argc <= 5, ESP_ERR_INVALID_ARG);

                // берем из system_settings_t thread.TLVs
                uint8_t dataset_tlvs_buf[254];
                uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);
                if (sys_settings.thread.TLVs[0] == '\0')
                {
                    ESP_LOGE(TAG, "Thread TLVs are not set");
                    if (!convert_hex_str_to_bytes(argv[1], dataset_tlvs_buf, dataset_tlvs_len))
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                }
                else if (!convert_hex_str_to_bytes(sys_settings.thread.TLVs, dataset_tlvs_buf, dataset_tlvs_len))
                {
                    return ESP_ERR_INVALID_ARG;
                }
                // если  аргументов передано больше 3, то берем их из argv
                uint32_t pincode;
                uint16_t disc;
                if (argc > 3)
                {
                    pincode = string_to_uint32(argv[2]);
                    disc = string_to_uint16(argv[3]);
                }
                else
                {
                    pincode = string_to_uint32(argv[1]);
                    disc = string_to_uint16(argv[2]);
                }

                result = controller::pairing_ble_thread(nodeId, pincode, disc, dataset_tlvs_buf, dataset_tlvs_len);
#else  // if !CONFIG_ENABLE_ESP32_BLE_CONTROLLER
            }
            else if (strncmp(argv[0], "ble-wifi", sizeof("ble-wifi")) == 0 ||
                     strncmp(argv[0], "ble-thread", sizeof("ble-thread")) == 0)
            {
                ESP_LOGE(TAG, "Please enable ENABLE_ESP32_BLE_CONTROLLER to use pairing %s command", argv[0]);
                return ESP_ERR_NOT_SUPPORTED;
#endif // CONFIG_ENABLE_ESP32_BLE_CONTROLLER
            }
            else if (strncmp(argv[0], "code", sizeof("code")) == 0)
            {
                ESP_LOGI(TAG, "Pairing over code command");
                VerifyOrReturnError(argc == 2, ESP_ERR_INVALID_ARG);
                // uint64_t nodeId = string_to_uint64(argv[1]);
                const char *payload = argv[1];

                result = controller::pairing_code(nodeId, payload);
            }
            else if (strncmp(argv[0], "code-thread", sizeof("code-thread")) == 0)
            {
                ESP_LOGI(TAG, "Pairing over code and thread command");
                VerifyOrReturnError(argc == 3, ESP_ERR_INVALID_ARG);
                // uint64_t nodeId = string_to_uint64(argv[1]);
                const char *payload = argv[2];
                uint8_t dataset_tlvs_buf[254];
                uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);
                if (!convert_hex_str_to_bytes(argv[1], dataset_tlvs_buf, dataset_tlvs_len))
                {
                    return ESP_ERR_INVALID_ARG;
                }

                result = controller::pairing_code_thread(nodeId, payload, dataset_tlvs_buf, dataset_tlvs_len);
            }
            else if (strncmp(argv[0], "code-wifi", sizeof("code-wifi")) == 0)
            {
                ESP_LOGI(TAG, "Pairing over code and wifi command");
                VerifyOrReturnError(argc == 4, ESP_ERR_INVALID_ARG);
                // uint64_t nodeId = string_to_uint64(argv[1]);
                const char *ssid = argv[1];
                const char *password = argv[2];
                const char *payload = argv[3];

                result = controller::pairing_code_wifi(nodeId, ssid, password, payload);
            }
            else if (strncmp(argv[0], "code-wifi-thread", sizeof("code-wifi-thread")) == 0)
            {
                ESP_LOGI(TAG, "Pairing over code, wifi and thread command");
                VerifyOrReturnError(argc == 5, ESP_ERR_INVALID_ARG);
                //  uint64_t nodeId = string_to_uint64(argv[1]);
                const char *ssid = argv[1];
                const char *password = argv[2];
                const char *payload = argv[3];

                uint8_t dataset_tlvs_buf[254];
                uint8_t dataset_tlvs_len = sizeof(dataset_tlvs_buf);
                if (!convert_hex_str_to_bytes(argv[4], dataset_tlvs_buf, dataset_tlvs_len))
                {
                    return ESP_ERR_INVALID_ARG;
                }

                result = controller::pairing_code_wifi_thread(nodeId, ssid, password, payload, dataset_tlvs_buf,
                                                              dataset_tlvs_len);
            }

            if (result != ESP_OK)
            {
                ESP_LOGE(TAG, "Pairing failed");
            }
            return result;
        }

#if CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONER_DISCOVERY
        esp_err_t controller_udc(int argc, char **argv)
        {
            if (argc < 1 || argc > 3)
            {
                return ESP_ERR_INVALID_ARG;
            }
            if (strncmp(argv[0], "reset", sizeof("reset")) == 0)
            {
                if (argc != 1)
                {
                    return ESP_ERR_INVALID_ARG;
                }
                controller::matter_controller_client::get_instance()
                    .get_commissioner()
                    ->GetUserDirectedCommissioningServer()
                    ->ResetUDCClientProcessingStates();
            }
            else if (strncmp(argv[0], "print", sizeof("print")) == 0)
            {
                if (argc != 1)
                {
                    return ESP_ERR_INVALID_ARG;
                }
                controller::matter_controller_client::get_instance()
                    .get_commissioner()
                    ->GetUserDirectedCommissioningServer()
                    ->PrintUDCClients();
            }
            else if (strncmp(argv[0], "commission", sizeof("commission")) == 0)
            {
                if (argc != 3)
                {
                    return ESP_ERR_INVALID_ARG;
                }
                uint32_t pincode = string_to_uint32(argv[1]);
                printf("pincode %ld", pincode);
                size_t index = (size_t)string_to_uint32(argv[2]);
                controller::matter_controller_client &instance = controller::matter_controller_client::get_instance();
                UDCClientState *state =
                    instance.get_commissioner()->GetUserDirectedCommissioningServer()->GetUDCClients().GetUDCClientState(index);
                ESP_RETURN_ON_FALSE(state != nullptr, ESP_FAIL, TAG, "UDC client not found");
                state->SetUDCClientProcessingState(chip::Protocols::UserDirectedCommissioning::UDCClientProcessingState::kCommissioningNode);

                chip::NodeId gRemoteId = chip::kTestDeviceNodeId;
                chip::RendezvousParameters params = chip::RendezvousParameters()
                                                        .SetSetupPINCode(pincode)
                                                        .SetDiscriminator(state->GetLongDiscriminator())
                                                        .SetPeerAddress(state->GetPeerAddress());
                do
                {
                    chip::Crypto::DRBG_get_bytes(reinterpret_cast<uint8_t *>(&gRemoteId), sizeof(gRemoteId));
                } while (!chip::IsOperationalNodeId(gRemoteId));

                ESP_RETURN_ON_FALSE(instance.get_commissioner()->PairDevice(gRemoteId, params) == CHIP_NO_ERROR, ESP_FAIL, TAG,
                                    "Failed to commission udc");
            }
            else
            {
                return ESP_ERR_INVALID_ARG;
            }
            return ESP_OK;
        }
#endif // CHIP_DEVICE_CONFIG_ENABLE_COMMISSIONER_DISCOVERY
#endif // CONFIG_ESP_MATTER_COMMISSIONER_ENABLE

#ifndef CONFIG_ESP_MATTER_ENABLE_MATTER_SERVER
        esp_err_t controller_group_settings(int argc, char **argv)
        {
            if (argc >= 1)
            {
                if (strncmp(argv[0], "show-groups", sizeof("show-groups")) == 0)
                {
                    return controller::group_settings::show_groups();
                }
                else if (strncmp(argv[0], "add-group", sizeof("add-group")) == 0)
                {
                    if (argc != 3)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    uint16_t group_id = string_to_uint16(argv[1]);
                    char *group_name = argv[2];
                    return controller::group_settings::add_group(group_name, group_id);
                }
                else if (strncmp(argv[0], "remove-group", sizeof("remove-group")) == 0)
                {
                    if (argc != 2)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    uint16_t group_id = string_to_uint16(argv[1]);
                    return controller::group_settings::remove_group(group_id);
                }
                else if (strncmp(argv[0], "show-keysets", sizeof("show-keysets")) == 0)
                {
                    return controller::group_settings::show_keysets();
                }
                else if (strncmp(argv[0], "add-keyset", sizeof("add-keyset")) == 0)
                {
                    if (argc != 5)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    uint16_t keyset_id = string_to_uint16(argv[1]);
                    uint8_t key_policy = string_to_uint8(argv[2]);
                    uint64_t validity_time = string_to_uint64(argv[3]);
                    char *epoch_key_oct_str = argv[4];
                    return controller::group_settings::add_keyset(keyset_id, key_policy, validity_time, epoch_key_oct_str);
                }
                else if (strncmp(argv[0], "remove-keyset", sizeof("remove_keyset")) == 0)
                {
                    if (argc != 2)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    uint16_t keyset_id = string_to_uint16(argv[1]);
                    return controller::group_settings::remove_keyset(keyset_id);
                }
                else if (strncmp(argv[0], "bind-keyset", sizeof("bind_keyset")) == 0)
                {
                    if (argc != 3)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    uint16_t group_id = string_to_uint16(argv[1]);
                    uint16_t keyset_id = string_to_uint16(argv[2]);
                    return controller::group_settings::bind_keyset(group_id, keyset_id);
                }
                else if (strncmp(argv[0], "unbind-keyset", sizeof("unbind_keyset")) == 0)
                {
                    if (argc != 3)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }
                    uint16_t group_id = string_to_uint16(argv[1]);
                    uint16_t keyset_id = string_to_uint16(argv[2]);
                    return controller::group_settings::unbind_keyset(group_id, keyset_id);
                }
            }
            ESP_LOGI(TAG, "Subcommands of group-settings:");
            ESP_LOGI(TAG, "Show groups   : controller group-settings show-groups");
            ESP_LOGI(TAG, "Add group     : controller group-settings add-group <group_id> <group_name>");
            ESP_LOGI(TAG, "Remove group  : controller group-settings remove-group <group_id>");
            ESP_LOGI(TAG, "Show keysets  : controller group-settings show-keysets");
            ESP_LOGI(TAG,
                     "Add keyset    : controller group-settings add-keyset <ketset_id> <policy> <validity_time> "
                     "<epoch_key_oct_str>");
            ESP_LOGI(TAG, "Remove keyset : controller group-settings remove-keyset <ketset_id>");
            ESP_LOGI(TAG, "Bind keyset   : controller group-settings bind-keyset <group_id> <ketset_id>");
            ESP_LOGI(TAG, "Unbind keyset : controller group-settings unbind-keyset <group_id> <ketset_id>");
            return ESP_OK;
        }
#endif

        void print_manual_code(const char *manual_code)
        {
            ESP_LOGI(TAG,
                     "*************************************Manual Code: [%s]**********************************************",
                     manual_code);
        }

        esp_err_t open_commissioning_window(int argc, char **argv)
        {
            if (argc != 5)
            {
                return ESP_ERR_INVALID_ARG;
            }
            uint64_t node_id = string_to_uint64(argv[0]);
            uint8_t option = string_to_uint8(argv[1]);
            bool is_enhanced = option == 1;
            uint16_t window_timeout = string_to_uint16(argv[2]);
            uint32_t iteration = string_to_uint32(argv[3]);
            uint16_t discriminator = string_to_uint16(argv[4]);

            controller::commissioning_window_opener::get_instance().set_callback(print_manual_code);
            return controller::commissioning_window_opener::get_instance().send_open_commissioning_window_command(
                node_id, is_enhanced, window_timeout, iteration, discriminator, 10000 /* timed_invoke_timeout_ms */);
        }

        esp_err_t controller_invoke_command(int argc, char **argv)
        {
            if (argc < 4)
            {
                return ESP_ERR_INVALID_ARG;
            }

            uint64_t node_id = string_to_uint64(argv[0]);
            uint16_t endpoint_id = string_to_uint16(argv[1]);
            uint32_t cluster_id = string_to_uint32(argv[2]);
            uint32_t command_id = string_to_uint32(argv[3]);

            if (argc > 5)
            {
                uint16_t timed_invoke_timeout_ms = string_to_uint16(argv[5]);
                if (timed_invoke_timeout_ms > 0)
                {
                    return controller::send_invoke_cluster_command(node_id, endpoint_id, cluster_id, command_id, argv[4],
                                                                   chip::MakeOptional(timed_invoke_timeout_ms));
                }
            }

            return controller::send_invoke_cluster_command(node_id, endpoint_id, cluster_id, command_id,
                                                           argc > 4 ? argv[4] : NULL);
        }

        // -------------------------- Чтение атрибутов с колбэками без  AttributePathParams -------------------------- //
        esp_err_t controller_request_attribute(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_or_event_id,
                                               esp_matter::controller::read_command_type_t command_type)
        {
            esp_matter::controller::read_command *cmd = chip::Platform::New<esp_matter::controller::read_command>(
                node_id, endpoint_id, cluster_id, attribute_or_event_id, command_type, OnAttributeData, OnReadDone, nullptr);
            if (!cmd)
            {
                ESP_LOGE(TAG, "Failed to alloc memory for read_command");
                return ESP_ERR_NO_MEM;
            }
            else
            {
                // chip::DeviceLayer::PlatformMgr().LockChipStack();
                esp_err_t err = cmd->send_command();
                // chip::DeviceLayer::PlatformMgr().UnlockChipStack();

                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to send read command: %s", esp_err_to_name(err));
                    return err;
                }
            }
            return ESP_OK;
        }
        // -------------------------- Чтение атрибутов с колбэками без  AttributePathParams -------------------------- //

        esp_err_t controller_read_attr(int argc, char **argv)
        {
            if (argc != 4)
            {
                return ESP_ERR_INVALID_ARG;
            }

            uint64_t node_id = string_to_uint64(argv[0]);
            ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
            ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
            ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
            ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], attribute_ids), TAG, "Failed to parse attribute IDs");

            // return controller::send_read_attr_command(node_id, endpoint_ids, cluster_ids, attribute_ids);
            // Добавляем колбэк для обработки отвера
            // Формируем массив AttributePathParams
            size_t attr_count = endpoint_ids.AllocatedSize() * cluster_ids.AllocatedSize() * attribute_ids.AllocatedSize();
            ScopedMemoryBufferWithSize<chip::app::AttributePathParams> attr_paths;
            attr_paths.Calloc(attr_count);
            if (!attr_paths.Get())
            {
                return ESP_ERR_NO_MEM;
            }
            size_t idx = 0;
            for (size_t e = 0; e < endpoint_ids.AllocatedSize(); ++e)
                for (size_t c = 0; c < cluster_ids.AllocatedSize(); ++c)
                    for (size_t a = 0; a < attribute_ids.AllocatedSize(); ++a)
                    {
                        attr_paths[idx].mEndpointId = endpoint_ids[e];
                        attr_paths[idx].mClusterId = cluster_ids[c];
                        attr_paths[idx].mAttributeId = attribute_ids[a];
                        ++idx;
                    }

            esp_matter::controller::read_command *cmd = chip::Platform::New<esp_matter::controller::read_command>(
                node_id, std::move(attr_paths), ScopedMemoryBufferWithSize<chip::app::EventPathParams>(),
                OnAttributeData, nullptr, nullptr);

            if (!cmd)
            {
                ESP_LOGE(TAG, "Failed to alloc memory for read_command");
                return ESP_ERR_NO_MEM;
            }
            else
            {
                //  chip::DeviceLayer::PlatformMgr().LockChipStack();
                return cmd->send_command();
                //  chip::DeviceLayer::PlatformMgr().UnlockChipStack();
                chip::Platform::Delete(cmd);
            }
        }

        esp_err_t controller_write_attr(int argc, char **argv)
        {
            if (argc < 5)
            {
                return ESP_ERR_INVALID_ARG;
            }

            uint64_t node_id = string_to_uint64(argv[0]);
            ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
            ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
            ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
            ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], attribute_ids), TAG, "Failed to parse attribute IDs");

            char *attribute_val_str = argv[4];

            if (argc > 5)
            {
                uint16_t timed_write_timeout_ms = string_to_uint16(argv[5]);
                if (timed_write_timeout_ms > 0)
                {
                    return controller::send_write_attr_command(node_id, endpoint_ids, cluster_ids, attribute_ids,
                                                               attribute_val_str, chip::MakeOptional(timed_write_timeout_ms));
                }
            }

            return controller::send_write_attr_command(node_id, endpoint_ids, cluster_ids, attribute_ids, attribute_val_str);
        }

        esp_err_t controller_read_event(int argc, char **argv)
        {
            if (argc != 4)
            {
                return ESP_ERR_INVALID_ARG;
            }

            uint64_t node_id = string_to_uint64(argv[0]);
            ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
            ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
            ScopedMemoryBufferWithSize<uint32_t> event_ids;
            ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], event_ids), TAG, "Failed to parse event IDs");

            return controller::send_read_event_command(node_id, endpoint_ids, cluster_ids, event_ids);
        }
        /*
                esp_err_t controller_subscribe_attr(int argc, char **argv)
                {
                    if (argc != 6)
                    {
                        return ESP_ERR_INVALID_ARG;
                    }

                    uint64_t node_id = string_to_uint64(argv[0]);
                    ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
                    ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
                    ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
                    ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
                    ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
                    ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], attribute_ids), TAG, "Failed to parse attribute IDs");
                    uint16_t min_interval = string_to_uint16(argv[4]);
                    uint16_t max_interval = string_to_uint16(argv[5]);

                    return controller::send_subscribe_attr_command(node_id, endpoint_ids, cluster_ids, attribute_ids, min_interval, max_interval);
                }
        */
        esp_err_t controller_subscribe_attr(int argc, char **argv)
        {
            if (argc != 6)
            {
                return ESP_ERR_INVALID_ARG;
            }

            uint64_t node_id = string_to_uint64(argv[0]);
            ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
            ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
            ScopedMemoryBufferWithSize<uint32_t> attribute_ids;
            ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], attribute_ids), TAG, "Failed to parse attribute IDs");

            size_t attr_count = endpoint_ids.AllocatedSize() * cluster_ids.AllocatedSize() * attribute_ids.AllocatedSize();
            ScopedMemoryBufferWithSize<chip::app::AttributePathParams> attr_paths;
            attr_paths.Calloc(attr_count);
            if (!attr_paths.Get())
            {
                return ESP_ERR_NO_MEM;
            }
            size_t idx = 0;
            for (size_t e = 0; e < endpoint_ids.AllocatedSize(); ++e)
                for (size_t c = 0; c < cluster_ids.AllocatedSize(); ++c)
                    for (size_t a = 0; a < attribute_ids.AllocatedSize(); ++a)
                    {
                        attr_paths[idx].mEndpointId = endpoint_ids[e];
                        attr_paths[idx].mClusterId = cluster_ids[c];
                        attr_paths[idx].mAttributeId = attribute_ids[a];
                        ++idx;
                    }

            uint16_t min_interval = string_to_uint16(argv[4]);
            uint16_t max_interval = string_to_uint16(argv[5]);

            esp_matter::controller::subscribe_command *cmd = chip::Platform::New<esp_matter::controller::subscribe_command>(
                node_id, std::move(attr_paths), ScopedMemoryBufferWithSize<chip::app::EventPathParams>(),
                min_interval, max_interval, true, OnAttributeData, nullptr, nullptr, nullptr);

            if (!cmd)
            {
                ESP_LOGE(TAG, "Failed to alloc memory for subscribe_command");
                return ESP_ERR_NO_MEM;
            }
            esp_err_t err = cmd->send_command();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to send subscribe command: %s", esp_err_to_name(err));
                return err;
            }
            ESP_LOGI(TAG, "subscribe_command sent successfully");
            // Добавляем атрибут влокальную структуру g_controller
            handle_attribute_report(
                &g_controller,             // controller
                string_to_uint64(argv[0]), // node_id
                string_to_uint16(argv[1]), // endpoint_id
                string_to_uint32(argv[2]), // cluster_id
                string_to_uint32(argv[3]), // attribute_id
                nullptr,                   // value
                true                       // need_subscribe
            );
            // chip::Platform::Delete(cmd);

            return ESP_OK;
        }

        esp_err_t controller_subscribe_event(int argc, char **argv)
        {
            if (argc != 6)
            {
                return ESP_ERR_INVALID_ARG;
            }

            uint64_t node_id = string_to_uint64(argv[0]);
            ScopedMemoryBufferWithSize<uint16_t> endpoint_ids;
            ScopedMemoryBufferWithSize<uint32_t> cluster_ids;
            ScopedMemoryBufferWithSize<uint32_t> event_ids;
            ESP_RETURN_ON_ERROR(string_to_uint16_array(argv[1], endpoint_ids), TAG, "Failed to parse endpoint IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[2], cluster_ids), TAG, "Failed to parse cluster IDs");
            ESP_RETURN_ON_ERROR(string_to_uint32_array(argv[3], event_ids), TAG, "Failed to parse event IDs");
            uint16_t min_interval = string_to_uint16(argv[4]);
            uint16_t max_interval = string_to_uint16(argv[5]);
            return controller::send_subscribe_event_command(node_id, endpoint_ids, cluster_ids, event_ids, min_interval,
                                                            max_interval);
        }

        esp_err_t controller_shutdown_subscription(int argc, char **argv)
        {
            if (argc != 2)
            {
                return ESP_ERR_INVALID_ARG;
            }
            uint64_t node_id = string_to_uint64(argv[0]);
            uint32_t subscription_id = string_to_uint32(argv[1]);
            return controller::send_shutdown_subscription(node_id, subscription_id);
        }

        esp_err_t controller_shutdown_subscriptions(int argc, char **argv)
        {
            if (argc != 1)
            {
                return ESP_ERR_INVALID_ARG;
            }
            uint64_t node_id = string_to_uint64(argv[0]);
            controller::send_shutdown_subscriptions(node_id);
            return ESP_OK;
        }

        esp_err_t controller_shutdown_all_subscriptions(int argc, char **argv)
        {
            if (argc != 0)
            {
                return ESP_ERR_INVALID_ARG;
            }
            controller::send_shutdown_all_subscriptions();
            return ESP_OK;
        }

    } // namespace console
} // namespace esp_matter
