idf_component_register(SRCS "bsp.c"
                      INCLUDE_DIRS "."
                      REQUIRES nvs_flash esp_timer)