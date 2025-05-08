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

// #include <esp_wifi.h>
#include <esp_event.h>
#include <esp_pm.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

// #include "lwip/err.h"
// #include "lwip/sockets.h"
// #include "lwip/sys.h"
// #include <lwip/netdb.h>

#include "bsp.h"
#include "provisioner.h"
#include "mdns_manager.h"
#include "double_reset.h"
#include "commander.h"
#include "sock.h"

#include "helpers.h"

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <esp_log.h>

#define TCP_PORT_MUTEX_TIMEOUT pdMS_TO_TICKS(1000)
#define SPI_MUTEX_TIMEOUT pdMS_TO_TICKS(1000)
#define DATA_READY_TIMEOUT pdMS_TO_TICKS(1000)

static const char *TAG = "main";

socket_instance_t response_socket;
spi_device_handle_t spi = NULL;

TaskHandle_t tcp_server_task_handle = NULL;
TaskHandle_t data_handler_task_handle = NULL;

static QueueHandle_t gpio_evt_queue = NULL;

SemaphoreHandle_t data_ready_semaphore = NULL;
SemaphoreHandle_t tcp_port_mutex = NULL;
SemaphoreHandle_t spi_mutex = NULL;

bool transmits_enabled = false;

uint8_t spi_rx_buffer[CONFIG_WP_DATA_RX_LENGTH + HEADER_LEN];

static void tcp_server_task(void *pvParameters);
static void data_handler_task(void *pvParameters);

static void IRAM_ATTR data_ready_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

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

    esp_log_level_set(TAG, LOG_LOCAL_LEVEL);

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
    ESP_ERROR_CHECK(gpio_hold_en(CONFIG_WP_GPIO_LINK_READY));

    gpio_cfg.intr_type = GPIO_INTR_POSEDGE;
    gpio_cfg.mode = GPIO_MODE_INPUT;
    // gpio_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_cfg.pin_bit_mask = (1ULL << CONFIG_WP_GPIO_DATA_READY);
    ESP_ERROR_CHECK(gpio_config(&gpio_cfg));

    // Create semaphore
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_evt_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    data_ready_semaphore = xSemaphoreCreateBinary();
    if (data_ready_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return;
    }

    tcp_port_mutex = xSemaphoreCreateMutex();
    if (tcp_port_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }
    spi_mutex = xSemaphoreCreateMutex();
    if (spi_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Create data handler
    xTaskCreate(data_handler_task, "data_handler", CONFIG_WP_HANDLER_STACK_SIZE, NULL, CONFIG_WP_HANDLER_PRIORITY, &data_handler_task_handle);
    if (data_handler_task_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create data handler task");
        return;
    }

    // Initialize interrupt
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(CONFIG_WP_GPIO_DATA_READY, data_ready_handler, (void *)CONFIG_WP_GPIO_DATA_READY));

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
        .mode = 1,
        .spics_io_num = CONFIG_WP_SPI_CS,
        .queue_size = 3,
        .cs_ena_pretrans = 16,
        .cs_ena_posttrans = 16,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(CONFIG_WP_SPI_INSTANCE - 1, &spi_cfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(CONFIG_WP_SPI_INSTANCE - 1, &dev_cfg, &spi));
    ESP_ERROR_CHECK(gpio_hold_en(CONFIG_WP_SPI_CS));

    // Start provisioning
#if CONFIG_WP_DOUBLE_RESET
    ESP_ERROR_CHECK(provisioner_start(reset_provisioning));
#else
    ESP_ERROR_CHECK(provisioner_start(false));
#endif
    ESP_ERROR_CHECK(provisioner_wait());

    // // Print wifi stats
    // print_wifi_stats();

    // Start but suspend TWT
    provisioner_twt_setup();

    // Start TCP server
    xTaskCreate(tcp_server_task, "tcp_server", CONFIG_WP_SERVER_STACK_SIZE, NULL, CONFIG_WP_SERVER_PRIORITY, &tcp_server_task_handle);
    if (tcp_server_task_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create server task");
        return;
    }

    ESP_LOGI(TAG, "Returning from app_main()");
}

static void tcp_server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "TCP server task started");

    uint8_t rx_buffer[CONFIG_WP_SERVER_RX_BUFFER_SIZE];

    socket_instance_t listen_sock = sock_create();
    if (listen_sock.mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create listen socket");
        vTaskDelete(NULL);
        return;
    }

    // Initialize and bind listening socket
    ESP_ERROR_CHECK(sock_init(&listen_sock));
    ESP_ERROR_CHECK(sock_listen(&listen_sock, INADDR_ANY, CONFIG_WP_SOCKET_PORT));

    // Initialize response socket
    ESP_ERROR_CHECK(sock_init(&response_socket));

    esp_err_t err = ESP_OK;
    while (1)
    {
        // Accept a connection
        err = sock_accept(&listen_sock, &response_socket);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to accept connection");
            continue;
        }

        provisioner_twt_suspend(1);

        // Clear data ready signal
        xSemaphoreTake(data_ready_semaphore, 0);

        bool run = true;
        while (run)
        {
            wulpus_command_header_t recv_header;

            // Receive command from the socket (blocking)
            size_t data_len = sizeof(rx_buffer);
            err = command_recv(&response_socket, &recv_header, rx_buffer, &data_len);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to receive header");
                continue;
            }

            switch (recv_header.command)
            {
            case SET_CONFIG:
                ESP_LOGI(TAG, "Received set config command");

                // FIXME: This could be made tidier in the connection callback, but it's the same in the nRF52 firmware
                ESP_ERROR_CHECK(gpio_hold_dis(CONFIG_WP_GPIO_LINK_READY));
                ESP_ERROR_CHECK(gpio_set_level(CONFIG_WP_GPIO_LINK_READY, 1));
                ESP_ERROR_CHECK(gpio_hold_en(CONFIG_WP_GPIO_LINK_READY));
                ESP_LOGI(TAG, "Link ready signal set");

                // Wait for data ready signal
                if (xSemaphoreTake(data_ready_semaphore, DATA_READY_TIMEOUT) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to take data ready semaphore");
                    break;
                }

                uint8_t spi_tx_buffer[804] = {0};
                memcpy(spi_tx_buffer, rx_buffer, data_len);

                ESP_LOGD(TAG, "Configuration package (%u bytes):", recv_header.data_length);
                for (size_t i = 0; i < recv_header.data_length; i++)
                {
                    ESP_LOGD(TAG, "  0x%02X ", spi_tx_buffer[i]);
                }

                // Send configuration via SPI to the device
                spi_transaction_t tx = {
                    .length = 804 * 8,
                    .tx_buffer = spi_tx_buffer,
                    .rx_buffer = NULL,
                };
                gpio_hold_dis(CONFIG_WP_SPI_CS);
                if (xSemaphoreTake(spi_mutex, SPI_MUTEX_TIMEOUT) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to take SPI mutex");
                    break;
                }
                esp_err_t ret = spi_device_transmit(spi, &tx);
                xSemaphoreGive(spi_mutex);
                gpio_hold_en(CONFIG_WP_SPI_CS);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Error occurred during SPI transmission: %s", esp_err_to_name(ret));
                    break;
                }
                else
                {
                    ESP_LOGI(TAG, "Configuration package sent successfully");
                }

                break;
            case GET_DATA:
                ESP_LOGW(TAG, "GET_DATA is not implemented");
                break;
            case PING:
                ESP_LOGI(TAG, "Received ping command");
                // Send response
                wulpus_command_header_t response = {
                    .magic = "wulpus",
                    .command = PONG,
                    .data_length = 4,
                };
                err = command_send(&response_socket, &response, "pong", 4);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to send ping response");
                    break;
                }
                break;
            case RESET:
                ESP_LOGI(TAG, "Received reset command");
                // Reset self
                esp_restart();
                break;
            case CLOSE:
                ESP_LOGI(TAG, "Received close command");
                // Exit loop
                run = false;
                break;
            case START_RX:
                ESP_LOGI(TAG, "Received start RX command");
                // Enable transmits
                transmits_enabled = true;

                // By now, MSP could have sent a data ready signal, but we missed it
                if (xSemaphoreTake(data_ready_semaphore, 0) == pdTRUE)
                {
                    // Push dummy event into queue, since handler had to ignore last valid one
                    uint32_t io_num = CONFIG_WP_GPIO_DATA_READY;
                    xQueueSend(gpio_evt_queue, &io_num, 0);
                }

                break;
            case STOP_RX:
                ESP_LOGI(TAG, "Received stop RX command");
                // Disable transmits
                transmits_enabled = false;
                break;
            }

            ESP_LOGI(TAG, "Command %s processed", command_name(recv_header.command));
        }

        // Close socket
        err = sock_close(&response_socket);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to close socket");
        }
        else
        {
            ESP_LOGI(TAG, "Socket closed successfully");
        }

        provisioner_twt_suspend(0);
    }

    vTaskDelete(NULL);
}

static void data_handler_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Data handler task started");

    uint32_t io_num;

    spi_transaction_t rx = {
        .length = CONFIG_WP_DATA_RX_LENGTH * 8,
        .tx_buffer = NULL,
        .rx_buffer = spi_rx_buffer + HEADER_LEN,
    };
    wulpus_command_header_t response = {
        .magic = "wulpus",
        .command = GET_DATA,
        .data_length = CONFIG_WP_DATA_RX_LENGTH,
    };
    memcpy(spi_rx_buffer, &response, HEADER_LEN);

    while (1)
    {
        // Wait for data ready signal
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY) == pdTRUE)
        {
            // Data is ready, handle it here
            ESP_LOGD(TAG, "Data ready signal received on GPIO %lu", io_num);

            // Give data ready semaphore
            xSemaphoreGive(data_ready_semaphore);

            // If socket is open, send data
            // Header: "data <length>"
            // Data: <data>
            if (transmits_enabled & (response_socket.fd >= 0))
            {
                // // Get current time
                // uint32_t current_time = esp_timer_get_time();

                // Read data from the device
                gpio_hold_dis(CONFIG_WP_SPI_CS);
                if (xSemaphoreTake(spi_mutex, SPI_MUTEX_TIMEOUT) != pdTRUE)
                {
                    ESP_LOGE(TAG, "Failed to take SPI mutex");
                    continue;
                }
                esp_err_t ret = spi_device_transmit(spi, &rx);
                xSemaphoreGive(spi_mutex);
                gpio_hold_en(CONFIG_WP_SPI_CS);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Error occurred during SPI reception: %s", esp_err_to_name(ret));
                    continue;
                }

                // uint32_t elapsed_time = esp_timer_get_time() - current_time;
                // ESP_LOGD(TAG, "SPI reception took %lu us", elapsed_time);

                // ESP_LOGD(TAG, "TRX ID: %u", *(uint8_t *)(spi_rx_buffer + HEADER_LEN + 1));
                // ESP_LOGD(TAG, "ACQ NR: %u", *(uint16_t *)(spi_rx_buffer + HEADER_LEN + 2));

                // uint32_t data_sum = 0;
                // for (int i = 0; i < CONFIG_WP_DATA_RX_LENGTH; i++)
                // {
                //     data_sum += *(uint8_t *)(spi_rx_buffer + HEADER_LEN + 3 + i);
                // }
                // ESP_LOGD(TAG, "Data sum: %lu", data_sum);

                // current_time = esp_timer_get_time();

                // int flag = 1;
                // setsockopt(response_socket.fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

                // Send header and data
                ret = sock_send(&response_socket, spi_rx_buffer, CONFIG_WP_DATA_RX_LENGTH + HEADER_LEN);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to send data: %s", esp_err_to_name(ret));
                    continue;
                }

                // ESP_LOGI(TAG, "Sent successfully");

                // elapsed_time = esp_timer_get_time() - current_time;
                // ESP_LOGD(TAG, "Data sending took  %lu us", elapsed_time);
            }
        }
    }
}
