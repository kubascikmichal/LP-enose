#pragma once

#include <stdint.h>
#include "bme690_shared_config.h"

/* ====================================================================
 * PLACEHOLDER MODEL PARAMETERS - NOT TRAINED ON REAL DATA.
 *
 * Everything in this file must be regenerated from a labeled fresh-air /
 * toilet-smell dataset before BME690_MODE_PRODUCTION's classification is
 * meaningful. Until then, classify() is wired to safely return "fresh air"
 * (label 0) in every backend below, so the device just never wakes the HP
 * core rather than waking on garbage data.
 * ==================================================================== */

// Per-feature min/max used to normalize a raw feature value into a common
// 0..1000 integer range before CENTROID/KNN distance comparisons. RF doesn't
// use this - its thresholds are trained directly on raw feature values.
typedef struct {
    int32_t min;
    int32_t max;
} bme690_feature_range_t;

// PLACEHOLDER ranges - rough guesses, not fitted to real data.
static const bme690_feature_range_t bme690_feature_range[BME690_NUM_FEATURES] = {
    // gas_resistance[0..2], as percent-of-baseline * 100 (100 = at baseline)
    {0, 300}, {0, 300}, {0, 300},
    // temperature (°C * 100), humidity (%RH * 100), pressure (Pa)
    {1500, 3500}, {2000, 8000}, {95000, 105000},
};

#if defined(BME690_CLASSIFIER_CENTROID)

// One centroid per class, in the same normalized 0..1000 space classify()
// computes at runtime.
// PLACEHOLDER - both all-zero, so every reading is equidistant and
// classify() must break the tie towards "fresh" (see main.c).
static const int32_t bme690_centroid_fresh[BME690_NUM_FEATURES]  = {0, 0, 0, 0, 0, 0};
static const int32_t bme690_centroid_toilet[BME690_NUM_FEATURES] = {0, 0, 0, 0, 0, 0};

#elif defined(BME690_CLASSIFIER_KNN)

#define BME690_KNN_MAX_EXAMPLES 16
#define BME690_KNN_K 3

typedef struct {
    int32_t features[BME690_NUM_FEATURES]; // normalized 0..1000
    uint8_t label;                         // 0 = fresh, 1 = toilet
} bme690_knn_example_t;

// PLACEHOLDER - empty training set.
#define BME690_KNN_NUM_EXAMPLES 0
static const bme690_knn_example_t bme690_knn_examples[BME690_KNN_MAX_EXAMPLES] = {{{0}, 0}};

#elif defined(BME690_CLASSIFIER_RF)

#define BME690_RF_MAX_TREES 5
#define BME690_RF_MAX_NODES 32

typedef struct {
    int8_t  feature_idx; // index into the RAW (non-normalized) feature vector; -1 = leaf
    int32_t threshold;   // raw units, same scale as that feature
    int8_t  left;         // child node index; at a leaf, the predicted label instead
    int8_t  right;        // child node index; unused at a leaf
} bme690_rf_node_t;

// PLACEHOLDER - zero trees.
#define BME690_RF_NUM_TREES 0
static const uint8_t bme690_rf_tree_root[1] = {0};
static const bme690_rf_node_t bme690_rf_nodes[1][1] = {{{-1, 0, 0, 0}}};

#else
#error "Define exactly one of BME690_CLASSIFIER_CENTROID / _KNN / _RF"
#endif
