#ifndef THREAD_UTIL_H
#define THREAD_UTIL_H

#include <esp_err.h>
#include <openthread/instance.h>

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief Initializes a new OpenThread dataset.
     *
     * This function initializes a new OpenThread dataset and returns the dataset in hex string format.
     *
     * @return A pointer to the hex string representation of the dataset.
     */
    const char *thread_dataset_init_new(void);

    /**
     * @brief Enables the OpenThread IPv6 interface.
     *
     * This function enables the IPv6 interface for the OpenThread stack.
     *
     * @return ESP_OK on success, ESP_FAIL on failure.
     */
    esp_err_t ifconfig_up(void);

    /**
     * @brief Disables the OpenThread IPv6 interface.
     *
     * This function disables the IPv6 interface for the OpenThread stack.
     *
     * @return ESP_OK on success, ESP_FAIL on failure.
     */
    esp_err_t ifconfig_down(void);

    /**
     * @brief Starts the OpenThread stack.
     *
     * This function enables the OpenThread stack.
     *
     * @return ESP_OK on success, ESP_FAIL on failure.
     */
    esp_err_t thread_start(void);

    /**
     * @brief Stops the OpenThread stack.
     *
     * This function disables the OpenThread stack.
     *
     * @return ESP_OK on success, ESP_FAIL on failure.
     */
    esp_err_t thread_stop(void);

#ifdef __cplusplus
}
#endif

#endif // THREAD_UTIL_H