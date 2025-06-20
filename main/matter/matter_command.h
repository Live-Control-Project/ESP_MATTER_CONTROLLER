#ifndef MATTER_COMMAND_H
#define MATTER_COMMAND_H
#include "esp_err.h"
#include <esp_matter_controller_read_command.h>

#ifdef __cplusplus
extern "C"
{
#endif
    namespace esp_matter
    {
        namespace command
        {
            esp_err_t controller_pairing(int argc, char **argv);
            esp_err_t controller_udc(int argc, char **argv);
            esp_err_t controller_group_settings(int argc, char **argv);
            esp_err_t open_commissioning_window(int argc, char **argv);
            esp_err_t controller_invoke_command(int argc, char **argv);
            esp_err_t controller_request_attribute(uint64_t node_id, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_or_event_id,
                                                   esp_matter::controller::read_command_type_t command_type);
            esp_err_t controller_read_attr(int argc, char **argv);
            esp_err_t controller_write_attr(int argc, char **argv);
            esp_err_t controller_read_event(int argc, char **argv);
            esp_err_t controller_subscribe_attr(int argc, char **argv);
            esp_err_t controller_subscribe_event(int argc, char **argv);
            esp_err_t controller_shutdown_subscription(int argc, char **argv);
            esp_err_t controller_shutdown_subscriptions(int argc, char **argv);
            esp_err_t controller_shutdown_all_subscriptions(int argc, char **argv);
        }
    }

#ifdef __cplusplus
}
#endif

#endif // MATTER_COMMAND_H