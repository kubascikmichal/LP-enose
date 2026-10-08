#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ulp_lp_core.h"
#include "lp_core_i2c.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "lp_core_main.h"
#include "lp_core/bme690_shared_config.h"

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

static void print_gas_scan(void)
{
    double temp_c = ulp_temperature / 100.0;
    double hum_pct = ulp_humidity / 100.0;
    double press_hpa = ulp_pressure / 100.0;

    printf("T=%.2fC H=%.2f%% P=%.2fhPa", temp_c, hum_pct, press_hpa);
    for (int i = 0; i < BME690_NUM_HEATER_STEPS; i++) {
        printf(" gas%d=%" PRIu32 "ohm(target=%" PRIu32 "C range=%" PRIu32 " valid=%" PRIu32 " stab=%" PRIu32 ")",
               i, ulp_gas_resistance[i], ulp_gas_heater_target_c[i],
               ulp_gas_range[i], ulp_gas_valid[i], ulp_heat_stab[i]);
    }
}

#if defined(BME690_MODE_LOGGING)

static void init_label_switch(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BME690_LABEL_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

// Switch closed to GND (LOW, via internal pull-up) = "toilet"; open (HIGH) = "fresh air".
static inline bool label_is_toilet(void)
{
    return gpio_get_level(BME690_LABEL_BUTTON_GPIO) == 0;
}

// Data-collection build: the HP core never deep-sleeps. It stays in a fast
// polling loop so a label-switch flip is never missed between the LP core's
// own ~5.75s measurement cycles, and prints one line per new sample for you
// to capture (e.g. `idf.py monitor | tee session.log`) and label offline.
void app_main(void)
{
    printf("Hello ESP32-C6 from lp_enose (LOGGING mode)\n");
    init_lp_core();
    init_lp_i2c();
    init_label_switch();

    uint32_t last_seq = UINT32_MAX;
    while (1) {
        uint32_t seq = ulp_sample_seq;
        if (seq != last_seq) {
            last_seq = seq;
            printf("seq=%" PRIu32 " label=%s ", seq, label_is_toilet() ? "toilet" : "fresh");
            print_gas_scan();
            printf("\n");
        }
        vTaskDelay(pdMS_TO_TICKS(75)); // ~13Hz poll - fast enough not to miss a switch flip
    }
}

#else // BME690_MODE_PRODUCTION

void app_main(void)
{
    printf("Hello ESP32-C6 from lp_enose (PRODUCTION mode)\n");
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_ULP) {
        init_lp_core();
        init_lp_i2c();
    } else {
        // The LP core only wakes us after BME690_DEBOUNCE_CYCLES consecutive
        // "toilet" classifications, so reaching here IS the detection event.
        printf("Toilet smell detected (label=%" PRIu32 ")\n", ulp_last_label);
        print_gas_scan();
        printf("\n  features:");
        for (int i = 0; i < BME690_NUM_FEATURES; i++) {
            printf(" %" PRId32, ulp_last_features[i]);
        }
        printf("\n");
    }
    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    esp_deep_sleep_start();
}

#endif
