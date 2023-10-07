#include "driver/temperature_sensor.h"

#ifdef CONFIG_SOC_TEMP_SENSOR_SUPPORTED
static temperature_sensor_handle_t temp_handle = nullptr;
#endif

void temp_init()
{
#ifdef CONFIG_SOC_TEMP_SENSOR_SUPPORTED
    temperature_sensor_config_t temp_sensor{};
    temp_sensor.range_min = -10;
    temp_sensor.range_max = 80;
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor, &temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
#endif
}

float temp_read()
{
    float celsius = -1.0;
#ifdef CONFIG_SOC_TEMP_SENSOR_SUPPORTED
    if (temp_handle != nullptr)
    {
        ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &celsius));  
    }
#endif
    return celsius;
}
