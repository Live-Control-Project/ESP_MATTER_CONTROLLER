#ifdef __cplusplus
extern "C" {
#endif

esp_err_t init_wifi_mqtt_handler(void);
void publis_status_mqtt(const char *topic, int EP, const char *deviceData);

#ifdef __cplusplus
}
#endif

