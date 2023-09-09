#pragma once

#include "esp_err.h"

esp_err_t camera_init();
unsigned int camera_get_frame_count(bool reset);

extern char camera_name[32];
