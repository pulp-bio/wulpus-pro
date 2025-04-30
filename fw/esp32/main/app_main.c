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

#include "index.h"

static const char *TAG = "main";

char *random_data;

// LED mutex
static SemaphoreHandle_t led_mutex;

const typedef enum e_status {
    STATUS_OFF,
    STATUS_PROVISIONING,
    STATUS_IDLE,
    STATUS_TRANSMITTING,
    STATUS_ERROR
} status_t;

static void tcp_server_task(void *pvParameters);
static void print_wifi_stats(void);
static void update_led(status_t status);

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_init());

#if CONFIG_WP_ENABLE_PM
    // Configure power management (DFS and auto light sleep)
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 10,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
#endif

    // Initialize LED
    led_mutex = xSemaphoreCreateMutex();
    update_led(STATUS_OFF);

    // Initialize provisioner and thus wifi
    ESP_ERROR_CHECK(provisioner_init());

    // Initialize mDNS
    ESP_ERROR_CHECK(mdns_manager_init("wulpus"));
    ESP_ERROR_CHECK(mdns_manager_add("wulpus", MDNS_PROTO_TCP, CONFIG_WP_SOCKET_PORT));

    update_led(STATUS_PROVISIONING);

    // Start provisioning
#if CONFIG_WP_RESET_PROVISIONED
    ESP_ERROR_CHECK(provisioner_start(true));
#else

    // // Set up button on IO45 to reset provisioning
    // gpio_config_t io_conf;
    // io_conf.intr_type = GPIO_INTR_DISABLE;
    // io_conf.mode = GPIO_MODE_INPUT;
    // io_conf.pin_bit_mask = 1ULL << 45;
    // io_conf.pull_down_en = 0;
    // io_conf.pull_up_en = 0;
    // gpio_config(&io_conf);

    // vTaskDelay(100 / portTICK_PERIOD_MS);

    // if (gpio_get_level(45) == 1)
    // {
    //     ESP_LOGW(TAG, "Resetting provisioning since button is pressed");
    //     ESP_ERROR_CHECK(provisioner_start(true));
    // }
    // else
    // {
    ESP_ERROR_CHECK(provisioner_start(false));
    // }
#endif
    ESP_ERROR_CHECK(provisioner_wait());

    // Print wifi stats
    print_wifi_stats();

    update_led(STATUS_IDLE);

    // Start TCP server
    xTaskCreate(tcp_server_task, "tcp_server", CONFIG_WP_SERVER_STACK_SIZE, NULL, CONFIG_WP_SERVER_PRIORITY, NULL);
}

typedef struct
{
    int fd;
    struct sockaddr_in addr;
    uint addr_len;
} socket_instance_t;

socket_instance_t response_socket;

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

        // Check if command is to start sending random data
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

        update_led(STATUS_IDLE);

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static void print_wifi_stats(void)
{
    // get wifi protocol
    uint8_t wifi_protocol = 0;
    int status = esp_wifi_get_protocol(WIFI_IF_STA, &wifi_protocol);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get wifi protocol: %d", status);
    }
    else
    {
        char enabled_protocols[50] = "";
        if (wifi_protocol & WIFI_PROTOCOL_11B)
        {
            strcat(enabled_protocols, "11b,");
        }
        if (wifi_protocol & WIFI_PROTOCOL_11G)
        {
            strcat(enabled_protocols, "11g,");
        }
        if (wifi_protocol & WIFI_PROTOCOL_11N)
        {
            strcat(enabled_protocols, "11n,");
        }
        if (wifi_protocol & WIFI_PROTOCOL_LR)
        {
            strcat(enabled_protocols, "LR,");
        }
        if (wifi_protocol & WIFI_PROTOCOL_11AX)
        {
            strcat(enabled_protocols, "11ax,");
        }

        int len = strlen(enabled_protocols);
        if (len > 0)
        {
            enabled_protocols[len - 1] = '\0';
            ESP_LOGI(TAG, "Enabled wifi protocols: %s", enabled_protocols);
        }
        else if (len == 0)
        {
            ESP_LOGW(TAG, "No wifi protocols enabled");
        }
    }

    wifi_phy_mode_t mode;
    status = esp_wifi_sta_get_negotiated_phymode(&mode);
    switch (mode)
    {
    case WIFI_PHY_MODE_11B:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: 11B");
        break;
    case WIFI_PHY_MODE_11G:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: 11G");
        break;
    case WIFI_PHY_MODE_HE20:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: HE20");
        break;
    case WIFI_PHY_MODE_HT20:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: HT20");
        break;
    case WIFI_PHY_MODE_HT40:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: HT40");
        break;
    case WIFI_PHY_MODE_LR:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: LR");
        break;
    case WIFI_PHY_MODE_11A:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: 11A");
        break;
    case WIFI_PHY_MODE_VHT20:
        ESP_LOGI(TAG, "Negotiated wifi phy mode: VHT20");
        break;
    }

    uint8_t primary_channel;
    wifi_second_chan_t second_channel;
    status = esp_wifi_get_channel(&primary_channel, &second_channel);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get wifi channel: %d", status);
    }
    else
    {
        ESP_LOGI(TAG, "Negotiated wifi channel: %d", primary_channel);
        switch (second_channel)
        {
        case WIFI_SECOND_CHAN_NONE:
            ESP_LOGI(TAG, "The channel width is HT20");
            break;
        case WIFI_SECOND_CHAN_ABOVE:
            ESP_LOGI(TAG, "The channel width is HT40 and the secondary channel is above the primary channel");
            break;
        case WIFI_SECOND_CHAN_BELOW:
            ESP_LOGI(TAG, "The channel width is HT40 and the secondary channel is below the primary channel");
            break;
        default:
            ESP_LOGW(TAG, "Unknown secondary channel");
            break;
        }
    }

    wifi_ps_type_t ps_type;
    status = esp_wifi_get_ps(&ps_type);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get wifi power save type: %d", status);
    }
    else
    {
        if (ps_type == WIFI_PS_NONE)
        {
            ESP_LOGI(TAG, "Enabled wifi power save type: NONE");
        }
        else if (ps_type == WIFI_PS_MIN_MODEM)
        {
            ESP_LOGI(TAG, "Enabled wifi power save type: MIN MODEM");
        }
        else if (ps_type == WIFI_PS_MAX_MODEM)
        {
            ESP_LOGI(TAG, "Enabled wifi power save type: MAX MODEM");
        }
    }
}

void toggle_led_callback(void *arg)
{
    static bool led_state = false;
    led_state = !led_state;
    // take mutex
    xSemaphoreTake(led_mutex, portMAX_DELAY);
    ESP_ERROR_CHECK(bsp_led_set(led_state ? BSP_COLOR_GREEN : BSP_COLOR_CYAN, BSP_DEFAULT_BRIGHTNESS));
    // give mutex
    xSemaphoreGive(led_mutex);
}

void update_led(status_t status)
{
    static esp_timer_handle_t toggle_led_timer;
    const esp_timer_create_args_t toggle_led_timer_args = {
        .callback = &toggle_led_callback,
        .name = "toggle_led_timer"};

    xSemaphoreTake(led_mutex, portMAX_DELAY);
    if (esp_timer_is_active(toggle_led_timer))
    {
        ESP_ERROR_CHECK(esp_timer_stop(toggle_led_timer));
    }

    switch (status)
    {
    case STATUS_OFF:
        ESP_ERROR_CHECK(bsp_led_set(BSP_COLOR_BLACK, BSP_DEFAULT_BRIGHTNESS));
        break;
    case STATUS_PROVISIONING:
        ESP_ERROR_CHECK(bsp_led_set(BSP_COLOR_YELLOW, BSP_DEFAULT_BRIGHTNESS));
        break;
    case STATUS_IDLE:
        ESP_ERROR_CHECK(bsp_led_set(BSP_COLOR_GREEN, BSP_DEFAULT_BRIGHTNESS));
        break;
    case STATUS_TRANSMITTING:
        // start blinking
        ESP_ERROR_CHECK(esp_timer_create(&toggle_led_timer_args, &toggle_led_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(toggle_led_timer, 500000));
        break;
    case STATUS_ERROR:
        ESP_ERROR_CHECK(bsp_led_set(BSP_COLOR_RED, BSP_DEFAULT_BRIGHTNESS));
        break;
    default:
        ESP_LOGW(TAG, "Unknown status");
        break;
    }
    xSemaphoreGive(led_mutex);
}