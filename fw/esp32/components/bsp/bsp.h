/**
 * @file bsp.h
 * @brief Board support package header file for the ESP32-S3-N16R8 from WeActStudio
 */

#ifndef _BSP_H
#define _BSP_H

#include "esp_err.h"

#define BSP_DEFAULT_BRIGHTNESS CONFIG_BSP_DEFAULT_LED_BRIGHTNESS

const typedef enum bsp_color_t {
    BSP_COLOR_BLACK = 0,
    BSP_COLOR_RED = 1 << 0,
    BSP_COLOR_GREEN = 1 << 1,
    BSP_COLOR_BLUE = 1 << 2,
    BSP_COLOR_YELLOW = BSP_COLOR_RED | BSP_COLOR_GREEN,
    BSP_COLOR_CYAN = BSP_COLOR_GREEN | BSP_COLOR_BLUE,
    BSP_COLOR_MAGENTA = BSP_COLOR_RED | BSP_COLOR_BLUE,
    BSP_COLOR_WHITE = BSP_COLOR_RED | BSP_COLOR_GREEN | BSP_COLOR_BLUE
} bsp_color_t;

/**
 * @brief Initialize the board support package
 *
 * @return esp_err_t
 */
esp_err_t bsp_init(void);

esp_err_t bsp_led_set(bsp_color_t color, uint8_t brightness);

#endif // _BSP_H