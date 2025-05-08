#ifndef SOCK_H
#define SOCK_H

#include <stdint.h>

#include "esp_err.h"

#include "lwip/sockets.h"

typedef struct
{
    int fd;

    struct sockaddr_in addr;
    uint16_t addr_len;

    SemaphoreHandle_t mutex;
    TickType_t mutex_timeout;

    bool persist;
} socket_instance_t;

socket_instance_t sock_create(void);

esp_err_t sock_init(socket_instance_t *sock);

esp_err_t sock_listen(socket_instance_t *sock, uint32_t address, uint16_t port);

esp_err_t sock_accept(socket_instance_t *sock, socket_instance_t *client_sock);
esp_err_t sock_close(socket_instance_t *sock);

esp_err_t sock_recv(socket_instance_t *sock, void *buffer, size_t *length);
esp_err_t sock_send(socket_instance_t *sock, const void *buffer, size_t length);

#endif
