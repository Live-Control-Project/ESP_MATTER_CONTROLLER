#include <esp_log.h>
#include <esp_check.h>
#include <openthread/dataset.h>
#include <openthread/dataset_ftd.h>
#include <openthread/instance.h>
#include <openthread/thread.h>
#include <openthread/platform/memory.h>
#include <openthread/platform/toolchain.h>
#include <esp_openthread_lock.h>

#include "thread_util.h"

#include <portmacro.h>
#include <string.h>

static const char *TAG = "THREAD_UTIL";

// Declare otInstance as a global variable
static otInstance *openThreadInstance = NULL;

static int hex_digit_to_int(char hex)
{
    if ('A' <= hex && hex <= 'F')
    {
        return 10 + hex - 'A';
    }
    if ('a' <= hex && hex <= 'f')
    {
        return 10 + hex - 'a';
    }
    if ('0' <= hex && hex <= '9')
    {
        return hex - '0';
    }
    return -1;
}

static size_t hex_string_to_binary(const char *hex_string, uint8_t *buf, size_t buf_size)
{
    int num_char = strlen(hex_string);

    if (num_char != buf_size * 2)
    {
        return 0;
    }
    for (size_t i = 0; i < num_char; i += 2)
    {
        int digit0 = hex_digit_to_int(hex_string[i]);
        int digit1 = hex_digit_to_int(hex_string[i + 1]);

        if (digit0 < 0 || digit1 < 0)
        {
            return 0;
        }
        buf[i / 2] = (digit0 << 4) + digit1;
    }

    return buf_size;
}

const char *thread_dataset_init_new()
{
    static char dataset_tlvs_hex[512]; // Buffer to store TLV hex string
    ESP_LOGI(TAG, "Initializing new OpenThread dataset");

    openThreadInstance = otInstanceInitSingle();
    if (openThreadInstance == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize OpenThread instance");
        return NULL;
    }

    otOperationalDataset dataset;
    memset(&dataset, 0, sizeof(otOperationalDataset));

    esp_openthread_lock_acquire(portMAX_DELAY);

#if CONFIG_OPENTHREAD_FTD
    if (otDatasetCreateNewNetwork(openThreadInstance, &dataset) != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "Failed to create a new OpenThread dataset");
        esp_openthread_lock_release();
        return NULL;
    }
#endif

    // Active timestamp
    dataset.mActiveTimestamp.mSeconds = 1;
    dataset.mActiveTimestamp.mTicks = 0;
    dataset.mActiveTimestamp.mAuthoritative = false;
    dataset.mComponents.mIsActiveTimestampPresent = true;

    // Channel, Pan ID, Network Name
    dataset.mChannel = CONFIG_OPENTHREAD_NETWORK_CHANNEL;
    dataset.mComponents.mIsChannelPresent = true;
    dataset.mPanId = CONFIG_OPENTHREAD_NETWORK_PANID;
    dataset.mComponents.mIsPanIdPresent = true;

    size_t len = strlen(CONFIG_OPENTHREAD_NETWORK_NAME);
    if (len > OT_NETWORK_NAME_MAX_SIZE)
    {
        ESP_LOGE(TAG, "Network name exceeds maximum size");
        esp_openthread_lock_release();
        return NULL;
    }
    memcpy(dataset.mNetworkName.m8, CONFIG_OPENTHREAD_NETWORK_NAME, len + 1);
    dataset.mComponents.mIsNetworkNamePresent = true;

    // Extended Pan ID
    len = hex_string_to_binary(CONFIG_OPENTHREAD_NETWORK_EXTPANID, dataset.mExtendedPanId.m8,
                               sizeof(dataset.mExtendedPanId.m8));
    if (len != sizeof(dataset.mExtendedPanId.m8))
    {
        ESP_LOGE(TAG, "Cannot convert OpenThread extended PAN ID");
        esp_openthread_lock_release();
        return NULL;
    }
    dataset.mComponents.mIsExtendedPanIdPresent = true;

    // Mesh Local Prefix
    otIp6Prefix prefix;
    memset(&prefix, 0, sizeof(otIp6Prefix));
    if (otIp6PrefixFromString(CONFIG_OPENTHREAD_MESH_LOCAL_PREFIX, &prefix) == OT_ERROR_NONE)
    {
        memcpy(dataset.mMeshLocalPrefix.m8, prefix.mPrefix.mFields.m8, sizeof(dataset.mMeshLocalPrefix.m8));
        dataset.mComponents.mIsMeshLocalPrefixPresent = true;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to parse mesh local prefix: %s", CONFIG_OPENTHREAD_MESH_LOCAL_PREFIX);
        esp_openthread_lock_release();
        return NULL;
    }

    // Network Key
    len = hex_string_to_binary(CONFIG_OPENTHREAD_NETWORK_MASTERKEY, dataset.mNetworkKey.m8,
                               sizeof(dataset.mNetworkKey.m8));
    if (len != sizeof(dataset.mNetworkKey.m8))
    {
        ESP_LOGE(TAG, "Cannot convert OpenThread master key");
        esp_openthread_lock_release();
        return NULL;
    }
    dataset.mComponents.mIsNetworkKeyPresent = true;

    // PSKc
    len = hex_string_to_binary(CONFIG_OPENTHREAD_NETWORK_PSKC, dataset.mPskc.m8, sizeof(dataset.mPskc.m8));
    if (len != sizeof(dataset.mPskc.m8))
    {
        ESP_LOGE(TAG, "Cannot convert OpenThread pre-shared commissioner key");
        esp_openthread_lock_release();
        return NULL;
    }
    dataset.mComponents.mIsPskcPresent = true;

    // Apply dataset
    if (otDatasetSetActive(openThreadInstance, &dataset) != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "Failed to set OpenThread active dataset");
        esp_openthread_lock_release();
        return NULL;
    }

    // **FIX: Use otOperationalDatasetTlvs struct instead of uint8_t buffer**
    otOperationalDatasetTlvs datasetTlvs;
    memset(&datasetTlvs, 0, sizeof(otOperationalDatasetTlvs));

    otDatasetConvertToTlvs(&dataset, &datasetTlvs);

    // Convert TLVs to hex string
    for (int i = 0; i < datasetTlvs.mLength; i++)
    {
        snprintf(&dataset_tlvs_hex[i * 2], 3, "%02X", datasetTlvs.mTlvs[i]);
    }

    esp_openthread_lock_release();

    ESP_LOGI(TAG, "Dataset TLVs: %s", dataset_tlvs_hex);
    return dataset_tlvs_hex;
}

// Enable OpenThread IPv6
esp_err_t ifconfig_up()
{
    if (openThreadInstance == NULL)
    {
        ESP_LOGE(TAG, "OpenThread instance is not initialized");
        return ESP_FAIL;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otIp6SetEnabled(openThreadInstance, true) != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "Failed to enable OpenThread IPv6 interface");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t ifconfig_down()
{
    if (openThreadInstance == NULL)
    {
        ESP_LOGE(TAG, "OpenThread instance is not initialized");
        return ESP_FAIL;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otIp6SetEnabled(openThreadInstance, false) != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "Failed to disable OpenThread IPv6 interface");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t thread_start()
{
    if (openThreadInstance == NULL)
    {
        ESP_LOGE(TAG, "OpenThread instance is not initialized");
        return ESP_FAIL;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otThreadSetEnabled(openThreadInstance, true) != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "Failed to enable OpenThread");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}

esp_err_t thread_stop()
{
    if (openThreadInstance == NULL)
    {
        ESP_LOGE(TAG, "OpenThread instance is not initialized");
        return ESP_FAIL;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);

    if (otThreadSetEnabled(openThreadInstance, false) != OT_ERROR_NONE)
    {
        ESP_LOGE(TAG, "Failed to disable OpenThread");
        esp_openthread_lock_release(); // Release the lock before returning
        return ESP_FAIL;
    }

    esp_openthread_lock_release();

    return ESP_OK;
}