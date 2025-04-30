#include "provisioner.h"

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_wifi_he.h>
#include <esp_event.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#define TAG "provisioner"

#define ITWT_SETUP_SUCCESS 1

const int PROVISIONER_DONE_EVENT = BIT0;
const int PROVISIONER_CONNECTED_EVENT = BIT1;

static EventGroupHandle_t provisioner_event_group = NULL;
static TaskHandle_t provisioner_task_handle = NULL;
static bool started = false;

/**
 * @brief Initialize Wi-Fi in station mode
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/**
 * @brief Event handler for provisioning events
 *
 * @param arg Event handler argument
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void provisioner_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
#ifdef CONFIG_PROVISIONER_RESET_ON_FAILURE
    static int retries;
#endif
    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id)
        {
        case WIFI_PROV_START:
        {
            // Provisioning started

            ESP_LOGI(TAG, "Provisioning started");
            break;
        }
        case WIFI_PROV_CRED_RECV:
        {
            // Credentials received from provisioning service

            // Get and log Wi-Fi credentials
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials"
                          "\n\tSSID     : %s\n\tPassword : %s",
                     (const char *)wifi_sta_cfg->ssid,
                     (const char *)wifi_sta_cfg->password);
            break;
        }
        case WIFI_PROV_CRED_FAIL:
        {
            // Provisioning failed to receive credentials from app

            // Log failure reason and reset provisioned credentials if too many retries have been attempted
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s\n\tPlease reset to factory and retry provisioning", (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");

#ifdef CONFIG_PROVISIONER_RESET_ON_FAILURE
            // Reset provisioned credentials on failure if too many retries have been attempted
            retries++;
            if (retries >= CONFIG_PROVISIONER_RESET_ON_FAILURE_TRIES)
            {
                ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting provisioned credentials");
                wifi_prov_mgr_reset_sm_state_on_failure();
                retries = 0;
            }
#endif
            break;
        }
        case WIFI_PROV_CRED_SUCCESS:
        {
            // Provisioning successful

            ESP_LOGI(TAG, "Provisioning successful");

#ifdef CONFIG_PROVISIONER_RESET_ON_FAILURE
            retries = 0;
#endif
            break;
        }
        case WIFI_PROV_END:
        {
            // Deinit provisioning manager since we don't need it anymore
            wifi_prov_mgr_deinit();

            // Restart Wi-Fi in station mode
            ESP_ERROR_CHECK(esp_wifi_stop());
            wifi_init_sta();

            // Signal provisioning task that we are done provisioning
            xEventGroupSetBits(provisioner_event_group, PROVISIONER_DONE_EVENT);

            break;
        }

        default:
            break;
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        // Wi-Fi started in station mode
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        // Wi-Fi connected with IP Address

        // Get and log IP Address
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP Address " IPSTR, IP2STR(&event->ip_info.ip));

        // Get and log SSID
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);
        ESP_LOGI(TAG, "Connected to SSID %s", (const char *)ap_info.ssid);

        wifi_phy_mode_t mode;
        ESP_ERROR_CHECK(esp_wifi_sta_get_negotiated_phymode(&mode));
        if (mode == WIFI_PHY_MODE_HE20)
        {
            ESP_LOGI(TAG, "Wi-Fi PHY mode is HE20, TWT may be supported");
            // Set up TWT
            wifi_twt_setup_config_t config = {
                .setup_cmd = TWT_REQUEST,
                .flow_id = 0,
                .twt_id = 0,
                .flow_type = 0,
                .min_wake_dura = 255,
                .wake_duration_unit = 0,
                .wake_invl_expn = 10,
                .wake_invl_mant = 512,
                .trigger = 1,
                .timeout_time_ms = 5000};
            esp_err_t status = esp_wifi_sta_itwt_setup(&config);
            if (status != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to set up TWT: %d", status);
            }
            else
            {
                ESP_LOGI(TAG, "TWT setup successful");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Wi-Fi PHY mode is not HE20, TWT isn't supported");
        }

        // Set Wi-Fi power save mode to max modem
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));

        // Signal provisioning task that we are connected, but only after the restart after provisioning
        if (xEventGroupGetBits(provisioner_event_group) & PROVISIONER_DONE_EVENT)
            xEventGroupSetBits(provisioner_event_group, PROVISIONER_CONNECTED_EVENT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        // Wi-Fi disconnected

        // Restart Wi-Fi in station mode
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    }
}

/**
 * @brief Convert TWT probe status to string
 *
 * @param status TWT probe status
 *
 * @return String representation of TWT probe status
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static const char *itwt_probe_status_to_str(wifi_itwt_probe_status_t status)
{
    switch (status)
    {
    case ITWT_PROBE_FAIL:
        return "itwt probe fail";
    case ITWT_PROBE_SUCCESS:
        return "itwt probe success";
    case ITWT_PROBE_TIMEOUT:
        return "itwt probe timeout";
    case ITWT_PROBE_STA_DISCONNECTED:
        return "Sta disconnected";
    default:
        return "Unknown status";
    }
}

/**
 * @brief Event handler for TWT events
 *
 * @param arg Event handler argument
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void itwt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_ITWT_SETUP:
        {
            // TWT setup event

            wifi_event_sta_itwt_setup_t *setup = (wifi_event_sta_itwt_setup_t *)event_data;
            if (setup->status == ITWT_SETUP_SUCCESS)
            {
                /* TWT Wake Interval = TWT Wake Interval Mantissa * (2 ^ TWT Wake Interval Exponent) */
                ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, flow_id:%d, %s, %s, wake_dura:%d, wake_dura_unit:%d, wake_invl_e:%d, wake_invl_m:%d", setup->config.twt_id,
                         setup->config.flow_id, setup->config.trigger ? "trigger-enabled" : "non-trigger-enabled", setup->config.flow_type ? "unannounced" : "announced",
                         setup->config.min_wake_dura, setup->config.wake_duration_unit, setup->config.wake_invl_expn, setup->config.wake_invl_mant);
                ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SETUP>target wake time:%lld, wake duration:%d us, service period:%d us", setup->target_wake_time, setup->config.min_wake_dura << (setup->config.wake_duration_unit == 1 ? 10 : 8),
                         setup->config.wake_invl_mant << setup->config.wake_invl_expn);
            }
            else
            {
                if (setup->status == ESP_ERR_WIFI_TWT_SETUP_TIMEOUT)
                {
                    ESP_LOGE(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, timeout of receiving twt setup response frame", setup->config.twt_id);
                }
                else if (setup->status == ESP_ERR_WIFI_TWT_SETUP_TXFAIL)
                {
                    ESP_LOGE(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, twt setup frame tx failed, reason: %d", setup->config.twt_id, setup->reason);
                }
                else if (setup->status == ESP_ERR_WIFI_TWT_SETUP_REJECT)
                {
                    ESP_LOGE(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, twt setup request was rejected, setup cmd: %d", setup->config.twt_id, setup->config.setup_cmd);
                }
                else
                {
                    ESP_LOGE(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, twt setup failed, status: %d", setup->config.twt_id, setup->status);
                }
            }

            break;
        }
        case WIFI_EVENT_ITWT_TEARDOWN:
        {
            // TWT teardown event

            wifi_event_sta_itwt_teardown_t *teardown = (wifi_event_sta_itwt_teardown_t *)event_data;
            ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_TEARDOWN>flow_id %d%s", teardown->flow_id, (teardown->flow_id == 8) ? "(all twt)" : "");

            break;
        }
        case WIFI_EVENT_ITWT_SUSPEND:
        {
            // TWT suspend event

            wifi_event_sta_itwt_suspend_t *suspend = (wifi_event_sta_itwt_suspend_t *)event_data;
            ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SUSPEND>status:%d, flow_id_bitmap:0x%x, actual_suspend_time_ms:[%lu %lu %lu %lu %lu %lu %lu %lu]",
                     suspend->status, suspend->flow_id_bitmap,
                     suspend->actual_suspend_time_ms[0], suspend->actual_suspend_time_ms[1], suspend->actual_suspend_time_ms[2], suspend->actual_suspend_time_ms[3],
                     suspend->actual_suspend_time_ms[4], suspend->actual_suspend_time_ms[5], suspend->actual_suspend_time_ms[6], suspend->actual_suspend_time_ms[7]);

            break;
        }
        case WIFI_EVENT_ITWT_PROBE:
        {
            // TWT probe event

            wifi_event_sta_itwt_probe_t *probe = (wifi_event_sta_itwt_probe_t *)event_data;
            ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_PROBE>status:%s, reason:0x%x", itwt_probe_status_to_str(probe->status), probe->reason);

            break;
        }
        default:
            break;
        }
    }
}

/**
 * @brief Initialize provisioning manager
 *
 * @return ESP_OK on success; any other value indicates an error
 *
 */
esp_err_t provisioner_init(void)
{
    esp_err_t status = ESP_OK;

    provisioner_event_group = xEventGroupCreate();

    // Default ESP wifi initalization stuff
    status = esp_netif_init();
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error initializing TCP/IP stack %d", status);
        return status;
    }

    status = esp_event_loop_create_default();
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error initializing event loop %d", status);
        return status;
    }

    // Register event handlers for provisioning and general Wi-Fi events
    status = esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &provisioner_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering provisioning event handler %d", status);
        return status;
    }
    status = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &provisioner_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering Wi-Fi event handler %d", status);
        return status;
    }
    status = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &provisioner_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering IP event handler %d", status);
        return status;
    }

    // Register TWT event handlers
    status = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_SETUP, &itwt_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering TWT event handler %d", status);
        return status;
    }
    status = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_TEARDOWN, &itwt_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering TWT event handler %d", status);
        return status;
    }
    status = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_SUSPEND, &itwt_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering TWT event handler %d", status);
        return status;
    }
    status = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_ITWT_PROBE, &itwt_event_handler, NULL);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error registering TWT event handler %d", status);
        return status;
    }

    return status;
}

/**
 * @brief Reset provisioning manager
 *
 * @return ESP_OK on success; any other value indicates an error
 *
 */
esp_err_t provisioner_reset(void)
{
    // Check if provisioner is started
    if (!started)
    {
        ESP_LOGE(TAG, "Provisioner not started");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t status = ESP_OK;

    // Reset provisioning manager
    status = wifi_prov_mgr_reset_provisioning();

    return status;
}

/**
 * @brief Get device provisioning name
 *
 * @param service_name Buffer to store device provisioning name
 * @param max Maximum length of device provisioning name
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void get_device_provisioning_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_WULPUS_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

/**
 * @brief Provisioning task
 *
 * @param pvParameter Task parameter
 *
 * @note This function is static and should not be used outside of this file
 *
 */
static void provisioner_task(void *pvParameter)
{
    (void)pvParameter;

    // Check if device is provisioned
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

    // If not provisioned, start provisioning service
    if (!provisioned)
    {
        ESP_LOGI(TAG, "Starting provisioning");

        // Get device name
        char service_name[19];
        get_device_provisioning_name(service_name, sizeof(service_name));

        // Set provisioning parameters
        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        const char *pop = CONFIG_PROVISIONER_POP;
        wifi_prov_security1_params_t *sec_params = pop;
        const char *service_key = NULL;

        // Start provisioning service
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *)sec_params, service_name, service_key));

        // Wait for provisioning to finish
        xEventGroupWaitBits(provisioner_event_group, PROVISIONER_DONE_EVENT, false, true, portMAX_DELAY);

        ESP_LOGI(TAG, "Provisioning finished");
    }
    else
    {
        ESP_LOGI(TAG, "Already provisioned");

        // Deinit provisioning manager since we don't need it anymore
        wifi_prov_mgr_deinit();

        // Signal provisioning task that we are done provisioning
        xEventGroupSetBits(provisioner_event_group, PROVISIONER_DONE_EVENT);

        wifi_init_sta();
    }

    vTaskDelete(NULL);
}

esp_err_t provisioner_start(bool reset)
{
    // Check if provisioner is already started
    if (provisioner_task_handle != NULL)
    {
        ESP_LOGE(TAG, "Provisioner already started");
        return ESP_ERR_INVALID_STATE;
    }

    // Check if provisioner is initialized
    if (provisioner_event_group == NULL)
    {
        ESP_LOGE(TAG, "Provisioner not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t status = ESP_OK;

    // Initialize Wi-Fi including netif with default config
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    status = esp_wifi_init(&cfg);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error initializing Wi-Fi %d", status);
        return status;
    }

    // Initialize provisioning manager
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE};
    status = wifi_prov_mgr_init(config);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Error initializing provisioning manager %d", status);
        return status;
    }
    started = true;

    // Reset provisioning manager if requested
    if (reset)
    {
        ESP_LOGI(TAG, "Resetting provisioning manager");

        status = provisioner_reset();
        if (status != ESP_OK)
        {
            ESP_LOGE(TAG, "Error resetting provisioning manager %d", status);
            return status;
        }
    }

    // Start provisioning task
    xTaskCreate(provisioner_task, "provisioner_task", 4096, NULL, 5, &provisioner_task_handle);

    return status;
}

esp_err_t provisioner_stop(void)
{
    esp_err_t status = ESP_OK;

    // Check if task is initialized and running
    if (provisioner_task_handle != NULL)
    {
        if (eTaskGetState(provisioner_task_handle) == eRunning)
            // Delete provisioning task
            vTaskDelete(provisioner_task_handle);

        provisioner_task_handle = NULL;
    }

    // Deinit provisioning manager
    wifi_prov_mgr_deinit();

    ESP_LOGD(TAG, "Provisioner stopped");

    return status;
}

esp_err_t provisioner_wait(void)
{
    // Check if provisioner is started
    if (provisioner_task_handle == NULL)
    {
        ESP_LOGE(TAG, "Provisioner not started");
        return ESP_ERR_INVALID_STATE;
    }

    // Wait for provisioning to finish
    xEventGroupWaitBits(provisioner_event_group, PROVISIONER_DONE_EVENT, false, true, portMAX_DELAY);
    xEventGroupWaitBits(provisioner_event_group, PROVISIONER_CONNECTED_EVENT, false, true, portMAX_DELAY);

    return ESP_OK;
}
