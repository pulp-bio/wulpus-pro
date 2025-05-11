#ifndef MDNS_MANAGER_H
#define MDNS_MANAGER_H

#include "esp_err.h"

/**
 * @brief Protocol type to use for mDNS service
 *
 */
const typedef enum {
    MDNS_PROTO_TCP,
    MDNS_PROTO_UDP
} mdns_protocol_t;

esp_err_t mdns_manager_init(char *hostname);
esp_err_t mdns_manager_add(const char *name, mdns_protocol_t protocol, uint16_t port);

#endif
