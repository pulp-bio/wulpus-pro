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
#include <driver/spi_master.h>

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
spi_device_handle_t spi;

TaskHandle_t tcp_server_task_handle = NULL;
TaskHandle_t data_handler_task_handle = NULL;
SemaphoreHandle_t data_ready_sem = NULL;

uint8_t spi_rx_buffer[CONFIG_WP_DATA_RX_LENGTH];

static void tcp_server_task(void *pvParameters);
static void data_handler_task(void *pvParameters);
static void data_ready_handler(void *arg);

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

    // Initialize GPIO
    gpio_config_t gpio_cfg = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << CONFIG_WP_GPIO_LINK_READY),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_WP_GPIO_LINK_READY, 0));

    gpio_cfg.intr_type = GPIO_INTR_POSEDGE;
    gpio_cfg.mode = GPIO_MODE_INPUT;
    gpio_cfg.pin_bit_mask = (1ULL << CONFIG_WP_GPIO_DATA_READY);
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    // Initialize interrupt
    gpio_install_isr_service(0);
    gpio_isr_handler_add(CONFIG_WP_GPIO_DATA_READY, data_ready_handler, NULL);

    // Initialize SPI
    spi_bus_config_t spi_cfg = {
        .miso_io_num = CONFIG_WP_SPI_MISO,
        .mosi_io_num = CONFIG_WP_SPI_MOSI,
        .sclk_io_num = CONFIG_WP_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CONFIG_WP_SPI_MAX_TRANSFER_SIZE,
    };
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = CONFIG_WP_SPI_CLOCK_SPEED,
        .mode = 0,
        .spics_io_num = CONFIG_WP_SPI_CS,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_WP_SPI_INSTANCE - 1, &spi_cfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_WP_SPI_INSTANCE - 1, &dev_cfg, &spi));

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

    // Set link ready signal
    // FIXME: This could be made tidier in the connection callback, but it's the same in the nRF52 firmware
    ESP_ERROR_CHECK(gpio_set_level(CONFIG_WP_GPIO_LINK_READY, 1));

    bsp_update_led(STATUS_IDLE);

    // Start TCP server
    xTaskCreate(tcp_server_task, "tcp_server", CONFIG_WP_SERVER_STACK_SIZE, NULL, CONFIG_WP_SERVER_PRIORITY, &tcp_server_task_handle);
    if (tcp_server_task_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create server task");
        return;
    }

    // Start data handler
    xTaskCreate(data_handler_task, "data_handler", CONFIG_WP_HANDLER_STACK_SIZE, NULL, CONFIG_WP_HANDLER_PRIORITY, &data_handler_task_handle);
    if (data_handler_task_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create data handler task");
        return;
    }
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

        bsp_update_led(STATUS_TRANSMITTING);

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
        if (strcmp(token, "config") == 0)
        {
            // Rest of the command is a configuration package (bytes, not a string)
            uint8_t *config = (uint8_t *)rx_buffer + strlen(token) + 1;
            int config_len = len - strlen(token) - 1;
            ESP_LOGI(TAG, "Received configuration package of length %d", config_len);

            // Send configuration via SPI to the device
            spi_transaction_t tx = {
                .length = config_len * 8,
                .tx_buffer = config,
                .rx_buffer = NULL,
            };
            esp_err_t ret = spi_device_transmit(spi, &tx);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Error occurred during SPI transmission: %s", esp_err_to_name(ret));
                bsp_update_led(STATUS_ERROR);
            }
            else
            {
                ESP_LOGI(TAG, "Configuration package sent successfully");
                bsp_update_led(STATUS_IDLE);
            }
        }
        else if (strcmp(token, "reset") == 0)
        {
            ESP_LOGI(TAG, "Received reset command");

            // Reset self
            esp_restart();
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

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static void data_handler_task(void *pvParameters)
{
    // Create semaphore
    data_ready_sem = xSemaphoreCreateBinary();
    if (data_ready_sem == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        // Wait for data ready signal
        if (xSemaphoreTake(data_ready_sem, portMAX_DELAY) == pdTRUE)
        {
            // Data is ready, handle it here
            ESP_LOGI(TAG, "Data ready signal received");
            bsp_update_led(STATUS_TRANSMITTING);

            // If socket is open, send data
            // Header: "data <length>"
            // Data: <data>
            if (response_socket.fd >= 0)
            {
                // Read data from the device
                spi_transaction_t rx = {
                    .length = CONFIG_WP_DATA_RX_LENGTH * 8,
                    .tx_buffer = NULL,
                    .rx_buffer = spi_rx_buffer,
                };
                if (rx.rx_buffer == NULL)
                {
                    ESP_LOGE(TAG, "Failed to allocate memory for SPI transaction");
                    continue;
                }

                esp_err_t ret = spi_device_transmit(spi, &rx);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Error occurred during SPI reception: %s", esp_err_to_name(ret));
                    continue;
                }

                // Send data to the socket
                int err = send(response_socket.fd, rx.rx_buffer, rx.length, 0);
                if (err < 0)
                {
                    ESP_LOGE(TAG, "Error occurred during sending: %s", strerror(errno));
                    continue;
                }
                ESP_LOGD(TAG, "Data sent to socket");
            }

            bsp_update_led(STATUS_IDLE);
        }

        vTaskDelete(NULL);
    }
}

static void IRAM_ATTR data_ready_handler(void *arg)
{
    xSemaphoreGiveFromISR(data_ready_sem, NULL);
}