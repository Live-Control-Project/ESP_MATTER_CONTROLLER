#ifndef MATTER_COMMAND_H
#define MATTER_COMMAND_H
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif
    esp_err_t controller_pairing(int argc, char **argv);
    esp_err_t controller_udc(int argc, char **argv);
    esp_err_t controller_group_settings(int argc, char **argv);
    esp_err_t open_commissioning_window(int argc, char **argv);
    esp_err_t controller_invoke_command(int argc, char **argv);
    esp_err_t controller_read_attr(int argc, char **argv);
    esp_err_t controller_write_attr(int argc, char **argv);
    esp_err_t controller_read_event(int argc, char **argv);
    esp_err_t controller_subscribe_attr(int argc, char **argv);
    esp_err_t controller_subscribe_event(int argc, char **argv);
    esp_err_t controller_shutdown_subscription(int argc, char **argv);
    esp_err_t controller_shutdown_subscriptions(int argc, char **argv);
    esp_err_t controller_shutdown_all_subscriptions(int argc, char **argv);
    esp_err_t string_to_uint16_array(const char *str, ScopedMemoryBufferWithSize<uint16_t> &uint16_array);
    esp_err_t string_to_uint32_array(const char *str, ScopedMemoryBufferWithSize<uint32_t> &uint32_array);

#ifdef __cplusplus
}
#endif

#endif // MATTER_COMMAND_H