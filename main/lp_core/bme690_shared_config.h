#pragma once

/* Shared between main/lp_core/main.c (LP core) and main/main.c (HP core).
 * Pure #defines/typedefs only - this file must stay includable by both the
 * LP-core minimal build and the normal HP-core ESP-IDF build. */

// ---- Build mode: define exactly one -----------------------------------
// LOGGING: HP core stays awake, polls a label switch, and prints every raw
//   feature every cycle. Use this to collect training data.
// PRODUCTION: LP core classifies on-device and only wakes the HP core after
//   BME690_DEBOUNCE_CYCLES consecutive "toilet" classifications.
#define BME690_MODE_LOGGING
// #define BME690_MODE_PRODUCTION

#if defined(BME690_MODE_LOGGING) && defined(BME690_MODE_PRODUCTION)
#error "Define exactly one of BME690_MODE_LOGGING / BME690_MODE_PRODUCTION"
#endif
#if !defined(BME690_MODE_LOGGING) && !defined(BME690_MODE_PRODUCTION)
#error "Define exactly one of BME690_MODE_LOGGING / BME690_MODE_PRODUCTION"
#endif

// ---- Classifier backend (PRODUCTION mode only): define exactly one ----
// Only the selected backend's code/data is compiled in - see model_params.h.
#define BME690_CLASSIFIER_CENTROID
// #define BME690_CLASSIFIER_KNN
// #define BME690_CLASSIFIER_RF

// ---- Sensor / scan configuration ---------------------------------------
// Shared so training data collection and on-device inference see identical
// timing (same heater schedule, same cycle period) - mismatched timing here
// would shift the gas_resistance distribution between training and deployment.
#define BME690_NUM_HEATER_STEPS 3

// Feature vector order used everywhere (classifier, logging, diagnostics):
// gas_resistance[0..2] (baseline-relative), temperature, humidity, pressure.
#define BME690_NUM_FEATURES (BME690_NUM_HEATER_STEPS + 3)

// ---- PRODUCTION-mode detection behavior --------------------------------
// Consecutive "toilet" classifications required before waking the HP core.
#define BME690_DEBOUNCE_CYCLES 3

// EMA divisor for the rolling fresh-air baseline (bigger = slower to adapt).
// Baseline only updates on cycles classified as fresh air.
#define BME690_BASELINE_EMA_DIVISOR 8

// ---- LOGGING-mode label switch ------------------------------------------
// TODO: change this if GPIO10 isn't free on your wiring. Must not be
// GPIO6/GPIO7 (used by the LP I2C bus to the BME690).
// Wiring assumed: switch to GND, pin configured with internal pull-up, so
// closed/LOW = "toilet", open/HIGH = "fresh air".
#define BME690_LABEL_BUTTON_GPIO 10
