#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(prospector_env_sensor, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/env_sensor_data_changed.h>

/* -------------------------------------------------------------------------
 * Device handles
 * -------------------------------------------------------------------------
 */
static const struct device *ccs811_dev = DEVICE_DT_GET(DT_NODELABEL(ccs811));
static const struct device *htu21d_dev = DEVICE_DT_GET(DT_NODELABEL(htu21d));

/* -------------------------------------------------------------------------
 * Sampling parameters
 * -------------------------------------------------------------------------
 */
#define SAMPLE_INTERVAL_MS   10000   /* 10 s — CCS811 measurement interval  */
#define STARTUP_DELAY_MS     30000   /* 30 s warm-up before first reading    */

/* -------------------------------------------------------------------------
 * IAQ formula
 *
 *   IAQ = clamp((co2_ppm - 400) / 16 + tvoc_ppb / 10, 0, 500)
 *
 * Thresholds:
 *   0–50   Good
 *   51–100 Moderate
 *   101–200 Poor
 *   >200   Bad
 * -------------------------------------------------------------------------
 */
static uint16_t compute_iaq(uint16_t co2_ppm, uint16_t tvoc_ppb)
{
    int32_t score = 0;

    if (co2_ppm > 400) {
        score += (int32_t)(co2_ppm - 400) / 16;
    }
    score += (int32_t)tvoc_ppb / 10;

    if (score < 0) {
        score = 0;
    } else if (score > 500) {
        score = 500;
    }

    return (uint16_t)score;
}

/* -------------------------------------------------------------------------
 * Sensor thread
 * -------------------------------------------------------------------------
 */
static void env_sensor_thread(void *d0, void *d1, void *d2)
{
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    if (!device_is_ready(ccs811_dev)) {
        LOG_ERR("CCS811 not ready");
        return;
    }

    if (!device_is_ready(htu21d_dev)) {
        LOG_ERR("HTU21D not ready");
        return;
    }

    LOG_INF("Environmental sensor thread started; waiting %d ms for warm-up",
            STARTUP_DELAY_MS);
    k_msleep(STARTUP_DELAY_MS);

    while (1) {
        struct sensor_value temp_val = {0};
        struct sensor_value hum_val  = {0};
        struct sensor_value co2_val  = {0};
        struct sensor_value tvoc_val = {0};

        /* --- HTU21D: fetch temperature & humidity first so CCS811 can
         *     use them for temperature compensation. --- */
        int rc = sensor_sample_fetch(htu21d_dev);
        if (rc != 0) {
            LOG_WRN("HTU21D fetch failed: %d", rc);
            k_msleep(SAMPLE_INTERVAL_MS);
            continue;
        }

        rc = sensor_channel_get(htu21d_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp_val);
        if (rc != 0) {
            LOG_WRN("HTU21D temp get failed: %d", rc);
            k_msleep(SAMPLE_INTERVAL_MS);
            continue;
        }

        rc = sensor_channel_get(htu21d_dev, SENSOR_CHAN_HUMIDITY, &hum_val);
        if (rc != 0) {
            LOG_WRN("HTU21D humidity get failed: %d", rc);
            k_msleep(SAMPLE_INTERVAL_MS);
            continue;
        }

        /* --- Write temperature + humidity compensation to CCS811 --- */
        rc = ccs811_envdata_update(ccs811_dev, &temp_val, &hum_val);
        if (rc != 0) {
            LOG_WRN("CCS811 env update failed: %d", rc);
            /* non-fatal — continue with uncompensated reading */
        }

        /* --- CCS811: fetch eCO2 & TVOC --- */
        rc = sensor_sample_fetch(ccs811_dev);
        if (rc != 0) {
            LOG_WRN("CCS811 fetch failed: %d", rc);
            k_msleep(SAMPLE_INTERVAL_MS);
            continue;
        }

        rc = sensor_channel_get(ccs811_dev, SENSOR_CHAN_CO2, &co2_val);
        if (rc != 0) {
            LOG_WRN("CCS811 CO2 get failed: %d", rc);
            k_msleep(SAMPLE_INTERVAL_MS);
            continue;
        }

        rc = sensor_channel_get(ccs811_dev, SENSOR_CHAN_VOC, &tvoc_val);
        if (rc != 0) {
            LOG_WRN("CCS811 TVOC get failed: %d", rc);
            k_msleep(SAMPLE_INTERVAL_MS);
            continue;
        }

        /* --- Convert sensor_value to integer units --- */
        uint16_t co2_ppm  = (uint16_t)co2_val.val1;
        uint16_t tvoc_ppb = (uint16_t)tvoc_val.val1;

        /* temp: sensor_value stores degrees in val1 + val2 (micro-degrees).
         * Convert to millidegrees: val1 * 1000 + val2 / 1000 */
        int16_t temp_mdeg = (int16_t)(temp_val.val1 * 1000 +
                                      temp_val.val2 / 1000);

        /* humidity: same layout, milli-percent */
        uint16_t hum_mpct = (uint16_t)(hum_val.val1 * 1000 +
                                       hum_val.val2 / 1000);

        uint16_t iaq = compute_iaq(co2_ppm, tvoc_ppb);

        LOG_DBG("CO2=%uppm TVOC=%uppb Temp=%d.%03dC Hum=%u.%03d%% IAQ=%u",
                co2_ppm, tvoc_ppb,
                temp_val.val1, temp_val.val2 / 1000,
                hum_val.val1,  hum_val.val2 / 1000,
                iaq);

        /* --- Raise ZMK event --- */
        raise_zmk_env_sensor_data_changed(
            (struct zmk_env_sensor_data_changed){
                .co2_ppm      = co2_ppm,
                .tvoc_ppb     = tvoc_ppb,
                .temp_mdeg    = temp_mdeg,
                .humidity_mpct = hum_mpct,
                .iaq_score    = iaq,
            });

        k_msleep(SAMPLE_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(env_sensor_tid,
                2048,
                env_sensor_thread,
                NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO,
                0,
                0);
