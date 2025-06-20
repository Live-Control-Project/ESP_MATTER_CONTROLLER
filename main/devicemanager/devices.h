#ifndef DEVICES_H
#define DEVICES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_matter.h"

#define CONTROLLER_MAGIC 0x4D415454

#ifdef __cplusplus
extern "C"
{
#endif

    // Структура атрибута
    typedef struct matter_attribute
    {
        uint32_t attribute_id;
        char attribute_name[32];
        esp_matter_attr_val_t current_value;
        bool is_subscribed;
    } matter_attribute_t;

    // Структура кластера
    typedef struct matter_cluster
    {
        uint32_t cluster_id;
        char cluster_name[32];
        bool is_client;
        matter_attribute_t *attributes;
        uint16_t attributes_count;
    } matter_cluster_t;

    // Структура endpoint
    typedef struct matter_endpoint
    {
        uint16_t endpoint_id;
        char endpoint_name[32];
        uint16_t clusters[16]; // Массив ID кластеров (увеличьте размер при необходимости)
        uint8_t cluster_count; // Количество кластеров в массиве
    } matter_endpoint_t;

    // Структура узла (устройства)
    typedef struct matter_node
    {
        uint64_t node_id;
        bool is_online;
        char model_name[32];
        char description[64];
        char vendor_name[32];
        uint32_t vendor_id;
        char firmware_version[32];
        uint16_t product_id;

        matter_endpoint_t *endpoints;
        uint16_t endpoints_count;

        matter_cluster_t *server_clusters;
        uint16_t server_clusters_count;

        matter_cluster_t *client_clusters;
        uint16_t client_clusters_count;

        struct matter_node *next;
    } matter_node_t;

    // Основная структура контроллера
    typedef struct
    {
        uint32_t magic;
        matter_node_t *nodes_list;
        uint16_t nodes_count;
        uint64_t controller_node_id;
        uint16_t fabric_id;
    } matter_controller_t;

    /**
     * @brief Инициализация контроллера
     *
     * @param controller Указатель на структуру контроллера
     * @param controller_node_id Node ID контроллера
     * @param fabric_id Fabric ID
     */
    void matter_controller_init(matter_controller_t *controller, uint64_t controller_node_id, uint16_t fabric_id);

    /**
     * @brief Поиск узла по ID
     *
     * @param controller Указатель на структуру контроллера
     * @param node_id Идентификатор узла для поиска
     * @return matter_node_t* Найденный узел или NULL
     */
    matter_node_t *find_node(matter_controller_t *controller, uint64_t node_id);

    /**
     * @brief Добавление нового узла
     *
     * @param controller Указатель на структуру контроллера
     * @param node_id Идентификатор нового узла
     * @param model_name Название модели устройства
     * @param vendor_name Название производителя
     * @return matter_node_t* Указатель на созданный узел или NULL при ошибке
     */
    matter_node_t *add_node(matter_controller_t *controller, uint64_t node_id, const char *model_name, const char *vendor_name);

    /**
     * @brief Добавление endpoint к узлу
     *
     * @param node Указатель на узел
     * @param endpoint_id Идентификатор endpoint
     * @param endpoint_name Имя endpoint (может быть NULL)
     * @return matter_endpoint_t* Указатель на созданный endpoint или NULL при ошибке
     */
    matter_endpoint_t *add_endpoint(matter_node_t *node, uint16_t endpoint_id, const char *endpoint_name);

    /**
     * @brief Добавление кластера к узлу
     *
     * @param node Указатель на узел
     * @param cluster_id Идентификатор кластера
     * @param cluster_name Имя кластера (может быть NULL)
     * @param is_client Флаг, является ли кластер клиентским
     * @return matter_cluster_t* Указатель на созданный кластер или NULL при ошибке
     */
    matter_cluster_t *add_cluster(matter_node_t *node, uint32_t cluster_id, const char *cluster_name, bool is_client);

    /**
     * @brief Добавление атрибута к кластеру
     *
     * @param cluster Указатель на кластер
     * @param attribute_id Идентификатор атрибута
     * @param attribute_name Имя атрибута (может быть NULL)
     * @return matter_attribute_t* Указатель на созданный атрибут или NULL при ошибке
     */
    matter_attribute_t *add_attribute(matter_cluster_t *cluster, uint32_t attribute_id, const char *attribute_name);

    /**
     * @brief Обработка отчета об атрибуте
     *
     * @param controller Указатель на структуру контроллера
     * @param node_id Идентификатор узла
     * @param endpoint_id Идентификатор endpoint
     * @param cluster_id Идентификатор кластера
     * @param attribute_id Идентификатор атрибута
     * @param value Указатель на значение атрибута
     */
    void handle_attribute_report(matter_controller_t *controller, uint64_t node_id,
                                 uint16_t endpoint_id, uint32_t cluster_id,
                                 uint32_t attribute_id, esp_matter_attr_val_t *value, std::optional<bool> need_subscribe = std::nullopt);

    /**
     * @brief Освобождение ресурсов контроллера
     *
     * @param controller Указатель на структуру контроллера
     */
    void matter_controller_free(matter_controller_t *controller);

    /**
     * @brief Логирование всей структуры контроллера
     *
     * @param controller Указатель на структуру контроллера
     */
    void log_controller_structure(const matter_controller_t *controller);

    /**
     * @brief Удаление устройства из контроллера
     *
     * @param controller Указатель на контроллер
     * @param node_id ID узла для удаления
     * @return esp_err_t ESP_OK при успехе, код ошибки при неудаче
     */
    esp_err_t remove_device(matter_controller_t *controller, uint64_t node_id);

    /**
     * @brief Логирование информации об узле
     *
     * @param node Указатель на узел
     */
    void log_node_info(const matter_node_t *node);

    /**
     * @brief Логирование информации о кластере
     *
     * @param cluster Указатель на кластер
     * @param is_client Флаг, является ли кластер клиентским
     */
    void log_cluster_info(const matter_cluster_t *cluster, bool is_client);

    esp_err_t save_devices_to_nvs(matter_controller_t *controller);
    esp_err_t load_devices_from_nvs(matter_controller_t *controller);
    void clear_devices_in_nvs();
    esp_err_t subscribe_all_marked_attributes(matter_controller_t *controller);
    esp_err_t publish_fd(matter_controller_t *controller, uint64_t node_id,
                         uint16_t endpoint_id, uint32_t cluster_id,
                         uint32_t attribute_id);

#ifdef __cplusplus
}
#endif

#endif // DEVICES_H