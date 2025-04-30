#include "mdns_manager.h"

#include <string.h>

#include <esp_log.h>
#include <esp_wifi.h>

#include "mdns.h"

#define TAG "mdns_manager"

// mDNS instance name
static char *instance_name = NULL;

/**
 * @brief Get mDNS name for device
 *
 * @param prefix Prefix to use for mDNS name
 * @param mdns_name String to store mDNS name in
 * @param max Maximum length of string
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void get_device_mdns_name(char *prefix, char *mdns_name, size_t max)
{
#if CONFIG_MDNS_MANAGER_POSTPENDMAC
    uint8_t eth_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(mdns_name, max, "%s%02X%02X%02X",
             prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
#else
    snprintf(mdns_name, max, "%s", prefix);
#endif
}

/**
 * @brief Initialize mDNS service
 *
 * @param hostname Hostname to use for mDNS service
 *
 * @return ESP_OK on success; any other value indicates an error
 *
 */
esp_err_t mdns_manager_init(char *hostname)
{
    // If mDNS service is already started, throw an error
    if (instance_name != NULL)
    {
        ESP_LOGE(TAG, "mDNS service already started");
        return ESP_FAIL;
    }

    esp_err_t status = ESP_OK;

    // Initialize mDNS
    status = mdns_init();
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "mdns_init failed: %d", status);
        return status;
    }

    // Get mDNS name
    char mdns_name[64];
    get_device_mdns_name(hostname, mdns_name, sizeof(mdns_name));
    instance_name = strdup(mdns_name);

    // Set mDNS hostname and instance name
    status = mdns_hostname_set(mdns_name);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "mdns_hostname_set failed: %d", status);
        return status;
    }
    status = mdns_instance_name_set(mdns_name);

    ESP_LOGI(TAG, "mDNS service started");

    return status;
}

/**
 * @brief Get protocol string for mDNS service
 *
 * @param protocol Protocol of the service (TCP or UDP)
 * @param proto String to store protocol in
 * @param max Maximum length of string
 *
 * @return ESP_OK on success; any other value indicates an error
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void get_protocol_string(mdns_protocol_t protocol, char *proto, size_t max)
{
    switch (protocol)
    {
    case MDNS_PROTO_TCP:
        snprintf(proto, max, "_tcp");
        break;
    case MDNS_PROTO_UDP:
        snprintf(proto, max, "_udp");
        break;
    default:
        // Currently only TCP and UDP are supported
        ESP_LOGE(TAG, "Invalid protocol: %d", protocol);
        ESP_ERROR_CHECK(ESP_FAIL);
        break;
    }
}

/**
 * @brief Add mDNS service
 *
 * @param name Name of the service
 * @param protocol Protocol of the service (TCP or UDP)
 * @param port Port of the service
 *
 * @return ESP_OK on success; any other value indicates an error
 *
 */
esp_err_t mdns_manager_add(const char *name, mdns_protocol_t protocol, uint16_t port)
{
    // If mDNS service is not started, throw an error
    if (instance_name == NULL)
    {
        ESP_LOGE(TAG, "mDNS service not started");
        return ESP_FAIL;
    }

    esp_err_t status = ESP_OK;

    // Get service and protocol strings for mDNS
    char service[64];
    char proto[64];
    snprintf(service, sizeof(service), "_%s", name);
    get_protocol_string(protocol, proto, sizeof(proto));

    // Add mDNS service
    status = mdns_service_add(NULL, service, proto, port, NULL, 0);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "mdns_service_add failed: %d", status);
        return status;
    }
    status = mdns_service_instance_name_set(service, proto, instance_name);

    ESP_LOGI(TAG, "mDNS service added: %s", name);

    return status;
}