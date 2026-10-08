#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ulp_lp_core.h"
#include "lp_core_i2c.h"
#include "esp_sleep.h"
#include "lp_core_main.h"

// Must match BME690_NUM_HEATER_STEPS in main/lp_core/main.c; there's no shared
// header between the two binaries, so this is kept in sync by hand.
#define BME690_NUM_HEATER_STEPS 3

extern const uint8_t lp_core_main_bin_start[] asm("_binary_lp_core_main_bin_start");
extern const uint8_t lp_core_main_bin_end[]   asm("_binary_lp_core_main_bin_end");

void init_lp_core(void)
{
    /* Set LP core wakeup source as the HP CPU */
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = 10000,
    };

    /* Load LP core firmware */
    ESP_ERROR_CHECK(ulp_lp_core_load_binary(lp_core_main_bin_start, (lp_core_main_bin_end - lp_core_main_bin_start)));

    /* Run LP core */
    ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));

    printf("LP core loaded with firmware and running successfully\n");
}

void init_lp_i2c(void)
{
    esp_err_t ret = ESP_OK;

    /* Initialize LP I2C with default configuration */
    const lp_core_i2c_cfg_t i2c_cfg = LP_CORE_I2C_DEFAULT_CONFIG();
    ret = lp_core_i2c_master_init(LP_I2C_NUM_0, &i2c_cfg);
    if (ret != ESP_OK) {
        printf("LP I2C init failed\n");
        abort();
    }

    printf("LP I2C initialized successfully\n");
}

void app_main(void)
{
    printf("Hello ESP32-C6 from lp_enose\n");
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_ULP) {
        init_lp_core();
        init_lp_i2c();
    } else {
        printf("Woke up from LP core\n");

        double temp_c = ulp_temperature / 100.0;
        double hum_pct = ulp_humidity / 100.0;
        double press_hpa = ulp_pressure / 100.0;

        printf("Temperature: %.2f C, Humidity: %.2f %%, Pressure: %.2f hPa\n", temp_c, hum_pct, press_hpa);

        // One gas resistance reading per heater step (the "gas scan"): BME690 cycles
        // the heater through several target temperatures each cycle, since different
        // VOCs shift resistance differently at different plate temperatures.
        for (int i = 0; i < BME690_NUM_HEATER_STEPS; i++) {
            printf("  Gas[%d] target=%" PRIu32 " C: %" PRIu32 " ohm (range=%" PRIu32 " valid=%" PRIu32 " heat_stab=%" PRIu32 ")\n",
                   i, ulp_gas_heater_target_c[i], ulp_gas_resistance[i],
                   ulp_gas_range[i], ulp_gas_valid[i], ulp_heat_stab[i]);
        }
    }
    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    esp_deep_sleep_start();
}
