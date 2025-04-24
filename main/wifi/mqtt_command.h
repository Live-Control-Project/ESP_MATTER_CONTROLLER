#ifndef MQTT_COMMAND_H
#define MQTT_COMMAND_H

#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 
 * 
 * @param event 
 */
void handle_mqtt_data(esp_mqtt_event_handle_t event);

#ifdef __cplusplus
}
#endif

#endif // MQTT_COMMAND_H