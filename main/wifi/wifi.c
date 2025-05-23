#include "wifi.h"
#include <esp_err.h>
#include <esp_log.h>
#include <string.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include "sdkconfig.h"
#include "settings.h"
#include "bus.h"
#include "mqtt.h"

#include <time.h>
#include "esp_sntp.h"


static const char *TAG = "WiFi";
#define APP_NAME "MatterController"

#define CHECK(x) do { \
    esp_err_t retval = (x); \
    if (retval != ESP_OK) { \
        ESP_LOGE(TAG, "Error in %s: %s", __FUNCTION__, esp_err_to_name(retval)); \
        return retval; \
    } \
} while(0)

// For functions returning pointers (NULL check)
#define CHECK_PTR(x) do { \
    if ((x) == NULL) { \
        ESP_LOGE(TAG, "Error in %s: returned NULL", __FUNCTION__); \
        return ESP_FAIL; \
    } \
} while(0)

// получаем MAC адрес
#define MAC_ADDR_SIZE 6
uint8_t mac_address[MAC_ADDR_SIZE] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
static char *get_mac_address()
{
    uint8_t mac[MAC_ADDR_SIZE];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    static char char_mac_address[18]; // 17 characters for MAC address and 1 for null terminator
    sprintf(char_mac_address, "%02x%02x%02x-%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // Convert the MAC address string to uppercase
    for (int i = 0; i < 17; i++)
    {
        char_mac_address[i] = toupper(char_mac_address[i]);
    }

    ESP_LOGI(TAG, "MAC address: %s", char_mac_address);
    return char_mac_address;
}
static void initialize_sntp(void)
{
        esp_sntp_stop();
    // First configure all settings before initialization
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_setservername(2, "ru.pool.ntp.org");
    
    // Only then initialize the SNTP client
    sntp_init();
    
    // Wait for synchronization
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    if (retry == retry_count) {
        ESP_LOGE(TAG, "Failed to get NTP time");
    } else {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        ESP_LOGI(TAG, "Current GMT time: %s", asctime(&timeinfo));
    }
}

static esp_netif_t *iface = NULL;
static void log_ip_info()
{

    if (!iface)
        return;

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(iface, &ip_info);

    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "IP address: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Netmask:    " IPSTR, IP2STR(&ip_info.netmask));
    ESP_LOGI(TAG, "Gateway:    " IPSTR, IP2STR(&ip_info.gw));
    ESP_LOGI(TAG, "--------------------------------------------------");
    sprintf(sys_settings.wifi.ip.ip, IPSTR, IP2STR(&ip_info.ip));
    esp_netif_dns_info_t dns;
    for (esp_netif_dns_type_t t = ESP_NETIF_DNS_MAIN; t < ESP_NETIF_DNS_MAX; t++)
    {
        esp_netif_get_dns_info(iface, t, &dns);
        ESP_LOGI(TAG, "DNS %d:      " IPSTR, t, IP2STR(&dns.ip.u_addr.ip4));
    }
    ESP_LOGI(TAG, "--------------------------------------------------");
    strcpy(sys_settings.wifi.STA_MAC, get_mac_address());
}

static void set_ip_info()
{
    esp_err_t res;

    if (!sys_settings.wifi.ip.dhcp || sys_settings.wifi.mode == WIFI_MODE_AP)
    {
        if (sys_settings.wifi.mode == WIFI_MODE_AP)
            esp_netif_dhcps_stop(iface);
        else
            esp_netif_dhcpc_stop(iface);

        esp_netif_ip_info_t ip_info;
        ip_info.ip.addr = ipaddr_addr(sys_settings.wifi.ip.ip);
        ip_info.netmask.addr = ipaddr_addr(sys_settings.wifi.ip.netmask);
        ip_info.gw.addr = ipaddr_addr(sys_settings.wifi.ip.gateway);
        res = esp_netif_set_ip_info(iface, &ip_info);
        if (res != ESP_OK)
            ESP_LOGW(TAG, "Error setting IP address %d (%s)", res, esp_err_to_name(res));

        if (sys_settings.wifi.mode == WIFI_MODE_AP)
            esp_netif_dhcps_start(iface);
    }

    esp_netif_dns_info_t dns;
    dns.ip.type = IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ipaddr_addr(sys_settings.wifi.ip.dns);
    res = esp_netif_set_dns_info(iface,
                                 sys_settings.wifi.mode != WIFI_MODE_AP && sys_settings.wifi.ip.dhcp
                                     ? ESP_NETIF_DNS_FALLBACK
                                     : ESP_NETIF_DNS_MAIN,
                                 &dns);
    if (res != ESP_OK)
        ESP_LOGW(TAG, "Error setting DNS address %d (%s)", res, esp_err_to_name(res));
}

static void wifi_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_err_t res = ESP_OK;
    switch (event_id)
    {
    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "WiFi started in access point mode");
        set_ip_info();
        log_ip_info();
//        bus_send_event(EVENT_WIFI_UP, NULL, 0);
        break;
    case WIFI_EVENT_STA_START:
        ESP_LOGI(TAG, "WiFi started in station mode, connecting...");
        if ((res = esp_wifi_connect()) != ESP_OK)
            ESP_LOGE(TAG, "WiFi error %d [%s]", res, esp_err_to_name(res));
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "WiFi connected to '%s'", sys_settings.wifi.sta.ssid);
        sys_settings.wifi.STA_connected = true;         
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        sys_settings.wifi.STA_connected = false;
        bus_send_event(EVENT_WIFI_DOWN, NULL, 0);
        if ((res = esp_wifi_connect()) != ESP_OK)
            ESP_LOGE(TAG, "WiFi error %d [%s]", res, esp_err_to_name(res));
        break;
    default:
        break;
    }
}

static void ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_err_t res = ESP_OK;
    switch (event_id)
    {
    case IP_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "WiFi got IP address");
        log_ip_info();
        bus_send_event(EVENT_WIFI_UP, NULL, 0);
        init_wifi_mqtt_handler();
        initialize_sntp();
        break;
    case IP_EVENT_STA_LOST_IP:
        ESP_LOGI(TAG, "WiFi lost IP address, reconnecting");
        bus_send_event(EVENT_WIFI_DOWN, NULL, 0);
        if ((res = esp_wifi_connect()) != ESP_OK)
            ESP_LOGE(TAG, "WiFi error %d [%s]", res, esp_err_to_name(res));
        break;
    default:
        break;
    }
}

static esp_err_t init_ap(void)
{
    ESP_LOGI(TAG, "Starting WiFi in access point mode");

    // Create default AP interface
    esp_netif_t *iface = esp_netif_create_default_wifi_ap();
    if (iface == NULL) {
        ESP_LOGE(TAG, "Failed to create default AP interface");
        return ESP_FAIL;
    }
    esp_netif_set_hostname(iface, APP_NAME);

    // Initialize WiFi with default config
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&init_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register WiFi event handler
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event handler register failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (strlen((const char *)sys_settings.wifi.ap.ssid) == 0) {
        strcpy((char *)sys_settings.wifi.ap.ssid, "MatterController");
        strcpy((char *)sys_settings.wifi.ap.password, "");
        sys_settings.wifi.ap.channel = 6;
        sys_settings.wifi.ap.authmode = WIFI_AUTH_OPEN;
        sys_settings.wifi.ap.max_connection = 4;
        ESP_LOGW(TAG, "Using default AP settings");
    }
    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid = "",
            .password = "",
            .ssid_len = 0,
            .channel = 0,
            .authmode = DEFAULT_WIFI_AP_AUTHMODE,
            .max_connection = 4,
            .pmf_cfg = {
                .required = false
            }
        }
    };
    
    // Ensure these copies are working
    strncpy((char *)wifi_cfg.ap.ssid, (const char *)sys_settings.wifi.ap.ssid, 32);
    wifi_cfg.ap.ssid_len = strlen((const char *)wifi_cfg.ap.ssid);
    wifi_cfg.ap.channel = sys_settings.wifi.ap.channel;
    if (!strlen((const char *)sys_settings.wifi.ap.password))
    wifi_cfg.ap.authmode = WIFI_AUTH_OPEN;
        
    // Only copy password if not open network
    if (wifi_cfg.ap.authmode != WIFI_AUTH_OPEN) {
        strncpy((char *)wifi_cfg.ap.password, 
                (const char *)sys_settings.wifi.ap.password, 
                64);
    }
    ESP_LOGI(TAG, "WiFi access point settings:");
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "SSID: %s", wifi_cfg.ap.ssid);
    ESP_LOGI(TAG, "Channel: %d", wifi_cfg.ap.channel);
    ESP_LOGI(TAG, "Auth mode: %s", (wifi_cfg.ap.authmode == WIFI_AUTH_OPEN) ? "Open" : "WPA2/PSK");
    ESP_LOGI(TAG, "Max connections: %d", wifi_cfg.ap.max_connection);
    ESP_LOGI(TAG, "--------------------------------------------------");

    // Set WiFi mode and start AP
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t init_sta()
{
    ESP_LOGI(TAG, "Starting WiFi in station mode, connecting to '%s'", sys_settings.wifi.sta.ssid);

    CHECK_PTR(iface = esp_netif_create_default_wifi_sta());
    CHECK(esp_netif_set_hostname(iface, APP_NAME));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    CHECK(esp_wifi_init(&cfg));

    CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_handler, NULL, NULL));
    CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_handler, NULL, NULL));
    CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &ip_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {0};
    memcpy(wifi_cfg.sta.ssid, sys_settings.wifi.sta.ssid, sizeof(wifi_cfg.sta.ssid));
    memcpy(wifi_cfg.sta.password, sys_settings.wifi.sta.password, sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = sys_settings.wifi.sta.threshold.authmode;

    ESP_LOGI(TAG, "WiFi station settings:");
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "SSID: %s", wifi_cfg.sta.ssid);
    ESP_LOGI(TAG, "--------------------------------------------------");

    CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_init()
{
    CHECK(esp_netif_init());
    CHECK(esp_event_loop_create_default());

    if (sys_settings.wifi.mode == WIFI_MODE_AP)
        CHECK(init_ap());
    else
        CHECK(init_sta());

    return ESP_OK;
}