#include "helpers.h"

#include <string.h>

#include <esp_wifi.h>

#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <esp_log.h>

static const char *TAG = "helpers";

void print_wifi_stats(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);

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

    // get wifi band mode
    wifi_band_mode_t band_mode;
    status = esp_wifi_get_band_mode(&band_mode);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get wifi band mode: %d", status);
    }
    else
    {
        switch (band_mode)
        {
        case WIFI_BAND_MODE_2G_ONLY:
            ESP_LOGI(TAG, "Enabled wifi band mode: 2.4GHz");
            break;
        case WIFI_BAND_MODE_5G_ONLY:
            ESP_LOGI(TAG, "Enabled wifi band mode: 5GHz");
            break;
        case WIFI_BAND_MODE_AUTO:
            ESP_LOGI(TAG, "Enabled wifi band mode: 2.4GHz + 5GHz");
            break;
        default:
            ESP_LOGW(TAG, "Unknown wifi band mode");
            break;
        }
    }

    // get wifi band
    wifi_band_t band;
    status = esp_wifi_get_band(&band);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get wifi band: %d", status);
    }
    else
    {
        switch (band)
        {
        case WIFI_BAND_2G:
            ESP_LOGI(TAG, "Enabled wifi band: 2.4GHz");
            break;
        case WIFI_BAND_5G:
            ESP_LOGI(TAG, "Enabled wifi band: 5GHz");
            break;
        default:
            ESP_LOGW(TAG, "Unknown wifi band");
            break;
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

    // get wifi bandwidth
    wifi_bandwidth_t bandwidth;
    status = esp_wifi_get_bandwidth(WIFI_IF_STA, &bandwidth);
    if (status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get wifi bandwidth: %d", status);
    }
    else
    {
        switch (bandwidth)
        {
        case WIFI_BW20:
            ESP_LOGI(TAG, "Enabled wifi bandwidth: 20MHz");
            break;
        case WIFI_BW40:
            ESP_LOGI(TAG, "Enabled wifi bandwidth: 40MHz");
            break;
        case WIFI_BW80:
            ESP_LOGI(TAG, "Enabled wifi bandwidth: 80MHz");
            break;
        case WIFI_BW160:
            ESP_LOGI(TAG, "Enabled wifi bandwidth: 160MHz");
            break;
        case WIFI_BW80_BW80:
            ESP_LOGI(TAG, "Enabled wifi bandwidth: 80+80MHz");
            break;
        default:
            ESP_LOGW(TAG, "Unknown wifi bandwidth");
            break;
        }
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