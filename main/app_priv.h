/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <matter_controller_device_mgr.h>

/** Default attribute values used by during initialization */
#define CONTROLLER_DEVICE_NAME "Matter Controller"
#define DEFAULT_POWER true

typedef void *app_driver_handle_t;

/** Initialize the button driver
 *
 * This initializes the button driver associated with the selected board.
 *
 * @param[in] user_data Custom user data that will be used in button toggle callback.
 *
 * @return Handle on success.
 * @return NULL in case of failure.
 */
app_driver_handle_t app_driver_button_init(void *user_data);

/** Driver Update
 *
 * This API should be called to update the driver for the attribute being updated.
 * This is usually called from the common `app_attribute_update_cb()`.
 *
 * @param[in] endpoint_id Endpoint ID of the attribute.
 * @param[in] cluster_id Cluster ID of the attribute.
 * @param[in] attribute_id Attribute ID of the attribute.
 * @param[in] val Pointer to `esp_matter_attr_val_t`. Use appropriate elements as per the value type.
 *
 * @return ESP_OK on success.
 * @return error in case of failure.
 */
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);

void on_device_list_update();

esp_err_t update_device_init();

// esp_err_t app_attribute_update_cb(esp_matter::attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
//                                   uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data);

//   esp_err_t app_identification_cb(esp_matter::identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
//                                 uint8_t effect_variant, void *priv_data);

//   void app_event_cb(const ChipDeviceEvent *event, intptr_t arg);
