#ifndef PROVISIONER_H
#define PROVISIONER_H

#include <stdbool.h>

#include "esp_err.h"

esp_err_t provisioner_init(void);
esp_err_t provisioner_reset(void);

esp_err_t provisioner_start(bool reset);
esp_err_t provisioner_stop(void);

esp_err_t provisioner_wait(void);

esp_err_t provisioner_twt_setup(void);
esp_err_t provisioner_twt_suspend(int time);

#endif
