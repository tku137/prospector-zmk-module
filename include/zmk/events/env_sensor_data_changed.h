#pragma once

#include <zephyr/kernel.h>
#include <zmk/event_manager.h>

struct zmk_env_sensor_data_changed {
    uint16_t co2_ppm;
    uint16_t tvoc_ppb;
    int16_t  temp_mdeg;  /* millidegrees Celsius, e.g. 24500 = 24.5 °C */
    uint16_t humidity_mpct; /* milli-percent, e.g. 42000 = 42.0 % */
    uint16_t iaq_score;  /* 0–500 derived IAQ score */
};

ZMK_EVENT_DECLARE(zmk_env_sensor_data_changed);
