#ifndef MEEF_BUS_H_
#define MEEF_BUS_H_

#include <stdint.h>       // For uint8_t
#include "esp_err.h"      // For esp_err_t
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#define BUS_QUEUE_LEN 5
#define BUS_TIMEOUT_MS 500
#define BUS_EVENT_DATA_SIZE 64

typedef enum
{
    EVENT_WIFI_UP = 0,
    EVENT_WIFI_DOWN,
    EVENT_BUTTON_PRESSED,
    EVENT_BUTTON_PRESSED_LONG,
    EVENT_BUTTON_RELEASED,
    EVENT_BUTTON_CLICKED,
    EVENT_TIMER,
    EVENT_THREAD_UP,
    EVENT_THREAD_DOWN,
    EVENT_THREAD_START,
    EVENT_THREAD_CAN_SLEEP
} event_type_t;

typedef struct
{
    event_type_t type;
    uint8_t data[BUS_EVENT_DATA_SIZE];
} event_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bus_init();
esp_err_t bus_send_event(event_type_t type, void *data, size_t size);
esp_err_t bus_receive_event(event_t *e, size_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* MEEF_BUS_H_ */