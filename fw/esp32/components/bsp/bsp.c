#include "bsp.h"

#include <nvs_flash.h>

#include "led_strip.h"

led_strip_handle_t led = NULL;

esp_err_t led_init(void);

esp_err_t bsp_init(void)
{
    esp_err_t status = ESP_OK;

    // Initialize the NVS partition
    /* Initialize NVS partition */
    status = nvs_flash_init();
    if (status == ESP_ERR_NVS_NO_FREE_PAGES || status == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /* NVS partition was truncated
         * and needs to be erased */
        status = nvs_flash_erase();
        if (status != ESP_OK)
        {
            return status;
        }

        /* Retry nvs_flash_init */
        status = nvs_flash_init();
        if (status != ESP_OK)
        {
            return status;
        }
    }

#ifdef CONFIG_BSP_INIT_LED
    // Initialize the onboard LED
    status = led_init();
    if (status != ESP_OK)
    {
        return status;
    }
#endif // CONFIG_BSP_INIT_LED

    return status;
}

esp_err_t bsp_led_set(bsp_color_t color, uint8_t brightness)
{
    if (led == NULL)
    {
        return ESP_OK;
    }

    esp_err_t status = ESP_OK;

    uint8_t red = (color & BSP_COLOR_RED) ? brightness : 0;
    uint8_t green = (color & BSP_COLOR_GREEN) ? brightness : 0;
    uint8_t blue = (color & BSP_COLOR_BLUE) ? brightness : 0;

    status = led_strip_set_pixel(led, 0, red, green, blue);
    if (status != ESP_OK)
    {
        return status;
    }

    status = led_strip_refresh(led);

    return status;
}

esp_err_t led_init(void)
{
    esp_err_t status = ESP_OK;

    // LED strip general initialization, according to your led board design
    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_BSP_LED_PIN,     // The GPIO that connected to the LED strip's data line
        .max_leds = 1,                            // The number of LEDs in the strip,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB, // Pixel format of your LED strip
        .led_model = LED_MODEL_WS2812,            // LED strip model
        .flags.invert_out = false,                // whether to invert the output signal
    };

    // LED strip backend configuration: RMT
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,      // different clock source can lead to different power consumption
        .resolution_hz = (10 * 1000 * 1000), // RMT counter clock frequency
        .flags.with_dma = false,             // DMA feature is available on ESP target like ESP32-S3
    };

    // Initialize the LED strip
    status = led_strip_new_rmt_device(&strip_config, &rmt_config, &led);
    if (status != ESP_OK)
    {
        return status;
    }

    // Clear LED strip (turn off all LEDs)
    status = led_strip_clear(led);

    return status;
}