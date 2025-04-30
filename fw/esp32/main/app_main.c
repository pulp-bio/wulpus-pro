/* Wi-Fi Provisioning Manager Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <driver/gpio.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "bsp.h"
#include "provisioner.h"
#include "mdns_manager.h"
#include "double_reset.h"

#include "helpers.h"

typedef struct
{
    int fd;
    struct sockaddr_in addr;
    uint addr_len;
} socket_instance_t;

static const char *TAG = "main";

socket_instance_t response_socket;

static void tcp_server_task(void *pvParameters);

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_init());

#if CONFIG_WP_DOUBLE_RESET
    // Check double reset
    bool reset_provisioning = false;
    ESP_ERROR_CHECK(double_reset_start(&reset_provisioning, CONFIG_WP_DOUBLE_RESET_TIMEOUT));
    if (reset_provisioning)
    {
        ESP_LOGI(TAG, "Double reset detected! Provisioning will be reset.");
    }
#endif

#if CONFIG_WP_ENABLE_PM
    // Configure power management (DFS and auto light sleep)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
#endif

    // Initialize LED moved to bsp component
    bsp_update_led(STATUS_OFF);

    // Initialize provisioner and thus wifi
    ESP_ERROR_CHECK(provisioner_init());

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_manager_init("wulpus"));
    ESP_ERROR_CHECK(mdns_manager_add("wulpus", MDNS_PROTO_TCP, CONFIG_WP_SOCKET_PORT));

    bsp_update_led(STATUS_PROVISIONING);

    // Start provisioning
#if CONFIG_WP_DOUBLE_RESET
    ESP_ERROR_CHECK(provisioner_start(reset_provisioning));
#else
    ESP_ERROR_CHECK(provisioner_start(false));
#endif
    ESP_ERROR_CHECK(provisioner_wait());

    // Print wifi stats
    print_wifi_stats();

    bsp_update_led(STATUS_IDLE);

    // Start TCP server
    xTaskCreate(tcp_server_task, "tcp_server", CONFIG_WP_SERVER_STACK_SIZE, NULL, CONFIG_WP_SERVER_PRIORITY, NULL);
}

static void tcp_server_task(void *pvParameters)
{
    char rx_buffer[CONFIG_WP_SERVER_RX_BUFFER_SIZE];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    // Set up listening socket
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CONFIG_WP_SOCKET_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create listening socket: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening socket created");

    // Bind listening socket
    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening socket bound, port %d", CONFIG_WP_SOCKET_PORT);

    // Start listening
    err = listen(listen_sock, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Error occurred during listen: %s", strerror(errno));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Listening");

    while (1)
    {
        // Accept a connection
        response_socket.addr_len = sizeof(response_socket.addr);
        response_socket.fd = accept(listen_sock, (struct sockaddr *)&response_socket.addr, (socklen_t *)&response_socket.addr_len);
        if (response_socket.fd < 0)
        {
            ESP_LOGE(TAG, "Unable to accept connection: %s", strerror(errno));
            break;
        }

        // Read data from the socket
        int len = recv(response_socket.fd, rx_buffer, sizeof(rx_buffer) - 1, 0);
        // Error occurred during receiving
        if (len < 0)
        {
            ESP_LOGE(TAG, "recv failed: %s", strerror(errno));
            break;
        }
        // Data received
        else
        {
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            ESP_LOGI(TAG, "Received %d bytes: '%s'", len, rx_buffer);

            if (len == 0)
            {
                ESP_LOGI(TAG, "No data received");
                break;
            }

            // TODO: Send response
            // // Send response
            // char *resp = "Hello, world!";
            // int err = send(sock, resp, strlen(resp), 0);
            // if (err < 0)
            // {
            //     ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            //     break;
            // }
            // ESP_LOGI(TAG, "Response sent");
        }

        // Check command
        char *token = strtok(rx_buffer, " ");
        if (strcmp(token, "start") == 0)
        {
            // TODO: Implement
        }
        else
        {
            ESP_LOGI(TAG, "Received unknown command '%s'", token);
        }

        err = close(response_socket.fd);
        if (err != 0)
        {
            ESP_LOGE(TAG, "Unable to close socket: errno %s", strerror(errno));
            break;
        }

        bsp_update_led(STATUS_IDLE);

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}
