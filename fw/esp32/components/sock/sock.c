#include "sock.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <esp_log.h>

#define TAG "sock"

#define SOCK_USE_MUTEX 0

#if SOCK_USE_MUTEX
#define SOCK_CHECK_FD(sock)                          \
    do                                               \
    {                                                \
        if ((sock)->fd < 0)                          \
        {                                            \
            ESP_LOGE(TAG, "Socket not initialized"); \
            return ESP_ERR_INVALID_STATE;            \
        }                                            \
    } while (0)

#define SOCK_MUTEX_TAKE(sock)                                               \
    do                                                                      \
    {                                                                       \
        if (xSemaphoreTake((sock)->mutex, (sock)->mutex_timeout) != pdTRUE) \
        {                                                                   \
            ESP_LOGE(TAG, "Failed to take mutex");                          \
            return ESP_ERR_TIMEOUT;                                         \
        }                                                                   \
    } while (0)

#define SOCK_MUTEX_GIVE(sock) \
    if (!(sock)->persist)     \
    xSemaphoreGive((sock)->mutex)

#else
#define SOCK_CHECK_FD(sock) (void)0
#define SOCK_MUTEX_TAKE(sock) (void)0
#define SOCK_MUTEX_GIVE(sock) (void)0
#endif

socket_instance_t sock_create(void)
{
    esp_log_level_set(TAG, LOG_LOCAL_LEVEL);

    ESP_LOGD(TAG, "Creating socket...");

    socket_instance_t sock = {
        .fd = -1,
        .addr = {0},
        .addr_len = 0,
        .mutex = xSemaphoreCreateMutex(),
        .mutex_timeout = pdMS_TO_TICKS(1000),
        .persist = false,
    };

    if (sock.mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return sock;
    }

    ESP_LOGD(TAG, "Socket created with mutex %p", sock.mutex);
    return sock;
}

esp_err_t sock_init(socket_instance_t *sock)
{
    ESP_LOGD(TAG, "Initializing socket...");

    int addr_family = AF_INET;
    int ip_protocol = IPPROTO_IP;

    sock->fd = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sock->fd < 0)
    {
        ESP_LOGE(TAG, "Unable to initialize socket: %s", strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Socket initialized");
    return ESP_OK;
}

esp_err_t sock_listen(socket_instance_t *sock, uint32_t address, uint16_t port)
{
    ESP_LOGD(TAG, "Start listening on %s:%d...", inet_ntoa(sock->addr.sin_addr), port);
    SOCK_CHECK_FD(sock);

    sock->addr.sin_addr.s_addr = htonl(address);
    sock->addr.sin_family = AF_INET;
    sock->addr.sin_port = htons(port);
    sock->addr_len = sizeof(sock->addr);

    int err = bind(sock->fd, (struct sockaddr *)&sock->addr, sock->addr_len);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: %s", strerror(errno));
        return ESP_FAIL;
    }

    err = listen(sock->fd, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Error occurred during listen: %s", strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Listening on %s:%d", inet_ntoa(sock->addr.sin_addr), port);
    return ESP_OK;
}

esp_err_t sock_accept(socket_instance_t *sock, socket_instance_t *client_sock)
{
    ESP_LOGD(TAG, "Accepting connection...");
    SOCK_CHECK_FD(sock);

    client_sock->addr_len = sizeof(client_sock->addr);
    client_sock->fd = accept(sock->fd, (struct sockaddr *)&client_sock->addr, (socklen_t *)&client_sock->addr_len);
    if (client_sock->fd < 0)
    {
        ESP_LOGE(TAG, "Unable to accept connection: %s", strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Accepted connection from %s:%d", inet_ntoa(client_sock->addr.sin_addr), ntohs(client_sock->addr.sin_port));
    return ESP_OK;
}

esp_err_t sock_close(socket_instance_t *sock)
{
    ESP_LOGD(TAG, "Closing socket...");
    SOCK_CHECK_FD(sock);

    close(sock->fd);
    sock->fd = -1;

    ESP_LOGD(TAG, "Closed socket");
    return ESP_OK;
}

esp_err_t sock_recv(socket_instance_t *sock, void *buffer, size_t *length)
{
    ESP_LOGD(TAG, "Receiving data...");
    SOCK_CHECK_FD(sock);

    SOCK_MUTEX_TAKE(sock);
    ssize_t len = recv(sock->fd, buffer, *length, 0);
    SOCK_MUTEX_GIVE(sock);

    if (len < 0)
    {
        ESP_LOGE(TAG, "Receive failed: %s", strerror(errno));
        return ESP_FAIL;
    }
    else if (len == 0)
    {
        ESP_LOGW(TAG, "No data received");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Received data (%d bytes)", len);
    *length = len;
    return ESP_OK;
}

esp_err_t sock_send(socket_instance_t *sock, const void *buffer, size_t length)
{
    ESP_LOGD(TAG, "Sending data...");
    SOCK_CHECK_FD(sock);

    SOCK_MUTEX_TAKE(sock);
    ssize_t len = send(sock->fd, buffer, length, 0);
    SOCK_MUTEX_GIVE(sock);

    if (len < 0)
    {
        ESP_LOGE(TAG, "Send failed: %s", strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sent data (%d bytes)", len);
    return ESP_OK;
}