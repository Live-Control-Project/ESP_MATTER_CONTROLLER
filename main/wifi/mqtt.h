#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Функция для отправки данных в MQTT
esp_err_t mqtt_publish_attribute_data(const char *topic, const char *data);

// Получаем указатель на клиент (если нужно напрямую)
void *get_mqtt_client();

esp_err_t init_wifi_mqtt_handler(void);
//void publis_status_mqtt(const char *topic, int EP, const char *deviceData);


#ifdef __cplusplus
}
#endif