#include "driver/temperature_sensor.h"

static temperature_sensor_handle_t temp_handle = nullptr;

void temp_init()
{
    temperature_sensor_config_t temp_sensor{};
    temp_sensor.range_min = -10;
    temp_sensor.range_max = 80;
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor, &temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
}

float temp_read()
{
    float celsius = -1.0;
    if (temp_handle != nullptr)
    {
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &celsius));  
    }
    return celsius;
}
