#include <stdbool.h>
#include "ulp_lp_core_print.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_i2c.h"

#define BME690_I2C_ADDR 0x76  // or 0x77
#define BME690_REG_DATA 0x1D

#define LP_I2C_TRANS_TIMEOUT_CYCLES 5000

// Gas scan: cycle the heater through several target temperatures per cycle
// instead of one fixed temperature. Different VOCs shift resistance differently
// at different plate temperatures, so this multi-point profile is what makes
// the gas reading useful for odor classification instead of just presence/absence.
#define BME690_NUM_HEATER_STEPS 3
static const int32_t bme690_heater_target_c[BME690_NUM_HEATER_STEPS] = {200, 300, 400};


typedef struct {
    uint16_t par_t1;
    int16_t  par_t2;
    int8_t   par_t3;

    uint16_t par_p1;
    int16_t  par_p2;
    int8_t   par_p3;
    int8_t   par_p4;
    int16_t  par_p5;
    int16_t  par_p6;
    int8_t   par_p7;
    int8_t   par_p8;
    int16_t  par_p9;
    int8_t   par_p10;
    int8_t   par_p11;

    uint16_t par_h1;
    int8_t   par_h2;
    int8_t   par_h3;
    int8_t   par_h4;
    int16_t  par_h5;
    int8_t   par_h6;

    int8_t   par_g1;
    int16_t  par_g2;
    int8_t   par_g3;
    uint8_t  res_heat_range;
    int8_t   res_heat_val;
} bme_calib_t;

static bme_calib_t calib;

/* Register layout and compensation formulas are BME690-specific (Bosch
 * BST-BME690-DS001-04), not the BME680/688 algorithm: the trim register
 * addresses, parameter widths (e.g. single-byte par_p7/par_p8) and the
 * pressure/humidity/gas math below all differ from that older sensor. */

void bme690_read_calib()
{
    uint8_t ca[22]; /* 0x8A..0x9F: par_t2, par_t3, par_p1..par_p11 */
    uint8_t cb[14]; /* 0xE1..0xEE: par_h1..par_h6, par_t1 */

    uint8_t reg = 0x8A;
    lp_core_i2c_master_write_read_device(
        LP_I2C_NUM_0, BME690_I2C_ADDR,
        &reg, 1, ca, sizeof(ca),
        LP_I2C_TRANS_TIMEOUT_CYCLES);

    reg = 0xE1;
    lp_core_i2c_master_write_read_device(
        LP_I2C_NUM_0, BME690_I2C_ADDR,
        &reg, 1, cb, sizeof(cb),
        LP_I2C_TRANS_TIMEOUT_CYCLES);

    // Temperature / pressure trim (0x8A-0x9F block)
    calib.par_t2 = (int16_t)((ca[1] << 8) | ca[0]);   // 0x8A/0x8B
    calib.par_t3 = (int8_t)ca[2];                      // 0x8C

    calib.par_p5 = (int16_t)((ca[5] << 8) | ca[4]);    // 0x8E/0x8F
    calib.par_p6 = (int16_t)((ca[7] << 8) | ca[6]);    // 0x90/0x91
    calib.par_p7 = (int8_t)ca[8];                      // 0x92
    calib.par_p8 = (int8_t)ca[9];                       // 0x93
    calib.par_p1 = (uint16_t)((ca[11] << 8) | ca[10]); // 0x94/0x95
    calib.par_p2 = (int16_t)((ca[13] << 8) | ca[12]);  // 0x96/0x97
    calib.par_p3 = (int8_t)ca[14];                      // 0x98
    calib.par_p4 = (int8_t)ca[15];                      // 0x99
    calib.par_p9 = (int16_t)((ca[19] << 8) | ca[18]);  // 0x9C/0x9D
    calib.par_p10 = (int8_t)ca[20];                     // 0x9E
    calib.par_p11 = (int8_t)ca[21];                     // 0x9F

    // Humidity trim + par_t1 (0xE1-0xEE block)
    uint8_t shared = cb[1]; // 0xE2: par_h1<3:0> / par_h5<7:4>
    calib.par_h1 = (uint16_t)(((uint16_t)cb[2] << 4) | (shared & 0x0F)); // 0xE2<3:0>/0xE3, unsigned 12-bit

    int16_t h5_raw = (int16_t)(((uint16_t)cb[0] << 4) | (shared >> 4));  // 0xE2<7:4>/0xE1, signed 12-bit
    if (h5_raw & 0x0800) h5_raw -= 0x1000;
    calib.par_h5 = h5_raw;

    calib.par_h2 = (int8_t)cb[3]; // 0xE4
    calib.par_h4 = (int8_t)cb[4]; // 0xE5
    calib.par_h3 = (int8_t)cb[5]; // 0xE6
    calib.par_h6 = (int8_t)cb[6]; // 0xE7

    calib.par_t1 = (uint16_t)((cb[9] << 8) | cb[8]); // 0xE9/0xEA

    calib.par_g2 = (int16_t)((cb[11] << 8) | cb[10]); // 0xEB/0xEC
    calib.par_g1 = (int8_t)cb[12];                     // 0xED
    calib.par_g3 = (int8_t)cb[13];                      // 0xEE

    // Heater calibration (needed to compute a real res_heat_x target below)
    uint8_t cc[3]; // 0x00..0x02: res_heat_val, (unused), res_heat_range<5:4>
    reg = 0x00;
    lp_core_i2c_master_write_read_device(
        LP_I2C_NUM_0, BME690_I2C_ADDR,
        &reg, 1, cc, sizeof(cc),
        LP_I2C_TRANS_TIMEOUT_CYCLES);

    calib.res_heat_val = (int8_t)cc[0];               // 0x00
    calib.res_heat_range = (cc[2] & 0x30) >> 4;        // 0x02<5:4>
}

// Integer res_heat_x formula per BST-BME690-DS001-04 (heater calibration
// section): converts a target hot-plate temperature into the register code
// for res_heat_x<7:0>. amb_temp/target_temp are in whole degrees Celsius.
static uint8_t calc_res_heat_x(int32_t target_temp, int32_t amb_temp)
{
    int32_t var1 = ((amb_temp * calib.par_g3) / 10) << 8;
    int32_t var2 = ((int32_t)calib.par_g1 + 784) *
                   ((((((int32_t)calib.par_g2 + 154009) * target_temp * 5) / 100) + 3276800) / 10);
    int32_t var3 = var1 + (var2 >> 1);
    int32_t var4 = var3 / ((int32_t)calib.res_heat_range + 4);
    int32_t var5 = (131 * (int32_t)calib.res_heat_val) + 65536;
    int32_t res_heat_x100 = ((var4 / var5) - 250) * 34;

    return (uint8_t)((res_heat_x100 + 50) / 100);
}

// Compensation per BST-BME690-DS001-04 (integer path). t_lin is an
// intermediate carried from temperature into the pressure calculation.
static int32_t calc_temp_comp(uint32_t temp_adc, int64_t *t_lin_out)
{
    int64_t var1 = ((int64_t)temp_adc << 4) - ((int64_t)256 * calib.par_t1);
    int64_t var2 = var1 * calib.par_t2;
    int64_t var3 = (var1 * var1) * calib.par_t3;
    int64_t var4 = (var2 * 262144) + var3;
    int64_t t_lin = var4 / 4294967296LL;

    *t_lin_out = t_lin;
    return (int32_t)((t_lin * 25) / 16384); // °C *100
}

static int64_t calc_press_comp(uint32_t press_adc_raw, int64_t t_lin)
{
    int64_t press_adc = (int64_t)press_adc_raw << 4;

    int64_t d1 = t_lin * t_lin;
    int64_t d2 = d1 >> 6;
    int64_t d3 = (d2 * t_lin) >> 8;
    int64_t d4 = ((int64_t)calib.par_p4 * d3) >> 5;
    int64_t d5 = ((int64_t)calib.par_p3 * d1) << 4;
    int64_t d6 = ((int64_t)calib.par_p2 * t_lin) << 22;
    int64_t offset = ((int64_t)calib.par_p1 << 47) + d4 + d5 + d6;

    d2 = ((int64_t)calib.par_p8 * d3) >> 5;
    d4 = ((int64_t)calib.par_p7 * d1) << 2;
    d5 = (((int64_t)calib.par_p6 - 16384) * t_lin) << 21;
    int64_t sensitivity = (((int64_t)calib.par_p5 - 16384) << 46) + d2 + d4 + d5;

    d1 = (sensitivity >> 24) * press_adc;
    d2 = (int64_t)calib.par_p10 * t_lin;
    d3 = d2 + ((int64_t)calib.par_p9 << 16);
    d4 = (d3 * press_adc) >> 13;

    d5 = (press_adc * (d4 / 10)) >> 9;
    d5 = d5 * 10;

    int64_t d6b = press_adc * press_adc;
    d2 = ((int64_t)calib.par_p11 * d6b) >> 16;
    d3 = (d2 * press_adc) >> 7;
    d4 = (offset / 4) + d1 + d5 + d3;

    return (int64_t)(((uint64_t)d4 * 25) >> 40); // Pa *100
}

// Returns humidity in Q22.10 format (value / 1024 = %RH)
static int32_t calc_hum_comp_q22_10(uint32_t hum_adc, int32_t temp_comp)
{
    int32_t t_fine = (int32_t)((((int64_t)temp_comp << 8) - 128) / 5);
    int64_t prev = t_fine - 76800;

    int64_t b_term = (((int64_t)hum_adc << 14)
                      - ((int64_t)calib.par_h1 << 20)
                      - ((int64_t)calib.par_h2 * prev)
                      + 16384) >> 15;

    int64_t c = (prev * calib.par_h4) >> 10;
    int64_t e = ((prev * calib.par_h3) >> 11) + 32768;
    int64_t f = (c * e) >> 10;
    int64_t g = f + 2097152;
    int64_t h_term = (g * calib.par_h5 + 8192) >> 14;

    int64_t var_h = b_term * h_term;
    var_h = var_h - ((((var_h >> 15) * (var_h >> 15)) >> 7) * calib.par_h6 >> 4);

    if (var_h < 0) var_h = 0;
    if (var_h > 419430400) var_h = 419430400;

    return (int32_t)(var_h >> 12);
}

static uint32_t calc_gas_resistance(uint16_t gas_adc, uint8_t gas_range)
{
    uint32_t var1 = ((uint32_t)262144) >> gas_range;
    int32_t var2 = (int32_t)gas_adc - 512;
    var2 *= 3;
    var2 = 4096 + var2;

    uint32_t calc_gas_res = (((uint32_t)10000) * var1) / (uint32_t)var2;
    return calc_gas_res * 100; // Ohms
}

// All attributes BME690 provides for a cycle: T/P/H once, plus one gas reading
// per heater step (BME690_NUM_HEATER_STEPS entries, indexed the same as
// bme690_heater_target_c / gas_heater_target_c below).
volatile uint32_t temperature;    // °C * 100
volatile uint32_t humidity;       // %RH * 100
volatile uint32_t pressure;       // Pa
// Note: these are uint32_t (not uint8_t) even though the real values are tiny,
// because the LP-core symbol export tool only emits a correct array declaration
// when every element is a full 4-byte word; a uint8_t[N] array exports as a
// single bogus scalar instead.
volatile uint32_t gas_resistance[BME690_NUM_HEATER_STEPS]; // Ohms; 0 if that step's gas_valid/heat_stab say it isn't trustworthy
volatile uint32_t gas_range[BME690_NUM_HEATER_STEPS];      // ADC range index 0-15 the sensor auto-selected per step
volatile uint32_t gas_valid[BME690_NUM_HEATER_STEPS];      // gas_valid_r: gas conversion completed normally
volatile uint32_t heat_stab[BME690_NUM_HEATER_STEPS];      // heat_stab_r: heater reached its target temperature in time
volatile int32_t  gas_heater_target_c[BME690_NUM_HEATER_STEPS]; // target plate temp per step, so the HP core doesn't need its own copy of the schedule

static void bme690_soft_reset()
{
    uint8_t cmd[2] = {0xE0, 0xB6}; // reset register
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );

    ulp_lp_core_delay_us(5000);  // 5 ms
}

static void bme690_init()
{
    uint8_t cmd[2];

    // ✅ 2. Humidity oversampling (os_hum = 16x)
    cmd[0] = 0x72;
    cmd[1] = 0x05;
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );


    // ✅ 3. Temperature + pressure oversampling (os_temp + os_pres = 16x)
    // + forced mode later
    cmd[0] = 0x74;
    cmd[1] = 0xB5;  // os_temp=16x, os_pres=16x, mode=sleep
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );


    // ✅ 4. Filter OFF + ODR none
    cmd[0] = 0x75;
    cmd[1] = 0x00;
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );


    // Per BST-BME690-DS001-04 section 3.6: configure gas_wait_x, then res_heat_x,
    // then nb_conv, and only then set run_gas=1 last. Doing run_gas=1 before the
    // heater steps are configured left heat_stab_r (and hence gas_adc) stuck at 0.

    // ✅ 5/6. Heater duration (~100 ms) + temperature for every scan step.
    // res_heat_x/gas_wait_x are sequential registers starting at 0x5A/0x64 (x=0..9).
    for (int i = 0; i < BME690_NUM_HEATER_STEPS; i++) {
        cmd[0] = 0x64 + i;
        cmd[1] = 0x59; // 100 ms
        lp_core_i2c_master_write_to_device(
            LP_I2C_NUM_0,
            BME690_I2C_ADDR,
            cmd, 2,
            LP_I2C_TRANS_TIMEOUT_CYCLES
        );

        cmd[0] = 0x5A + i;
        cmd[1] = calc_res_heat_x(bme690_heater_target_c[i], 25); // assumed 25°C ambient
        lp_core_i2c_master_write_to_device(
            LP_I2C_NUM_0,
            BME690_I2C_ADDR,
            cmd, 2,
            LP_I2C_TRANS_TIMEOUT_CYCLES
        );

        gas_heater_target_c[i] = bme690_heater_target_c[i];
    }

    // ✅ 7. run_gas = 1, nb_conv = 0 for now; bmp690_read_data() rewrites nb_conv
    // before each step's trigger to select that step's res_heat_x/gas_wait_x.
    cmd[0] = 0x71;
    cmd[1] = 0x20;  // run_gas = 1 (bit 5), nb_conv = 0
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );
}

void bme690_trigger_measurement()
{
    uint8_t cmd[2];

    // Humidity oversampling
    cmd[0] = 0x72;
    cmd[1] = 0x01;
    lp_core_i2c_master_write_to_device(LP_I2C_NUM_0, BME690_I2C_ADDR, cmd, 2, LP_I2C_TRANS_TIMEOUT_CYCLES);

    // Temp + pressure oversampling + FORCED MODE
    cmd[0] = 0x74;
    cmd[1] = 0x25;
    lp_core_i2c_master_write_to_device(LP_I2C_NUM_0, BME690_I2C_ADDR, cmd, 2, LP_I2C_TRANS_TIMEOUT_CYCLES);
}


void bmp690_read_data(volatile uint32_t *temperature, volatile uint32_t *humidity, volatile uint32_t *pressure,
                       volatile uint32_t gas_resistance_out[BME690_NUM_HEATER_STEPS],
                       volatile uint32_t gas_range_out[BME690_NUM_HEATER_STEPS],
                       volatile uint32_t gas_valid_out[BME690_NUM_HEATER_STEPS],
                       volatile uint32_t heat_stab_out[BME690_NUM_HEATER_STEPS])
{
    for (int step = 0; step < BME690_NUM_HEATER_STEPS; step++) {
        // ✅ 1. Select this step's heater profile (nb_conv) and trigger forced measurement.
        // run_gas stays 1; nb_conv picks which res_heat_x/gas_wait_x the sensor uses.
        uint8_t gas1[2] = {0x71, (uint8_t)(0x20 | step)};
        lp_core_i2c_master_write_to_device(
            LP_I2C_NUM_0,
            BME690_I2C_ADDR,
            gas1, 2,
            LP_I2C_TRANS_TIMEOUT_CYCLES
        );

        uint8_t cmd[2] = {0x74, 0xB5 | 0x01};   // forced mode
        lp_core_i2c_master_write_to_device(
            LP_I2C_NUM_0,
            BME690_I2C_ADDR,
            cmd, 2,
            LP_I2C_TRANS_TIMEOUT_CYCLES
        );

        // ✅ 2. WAIT properly (VERY IMPORTANT)
        // TPH at 16x oversampling (~40ms) + 100ms heater/gas conversion; add margin.
        ulp_lp_core_delay_us(250000);

        // ✅ 3. Read raw data (field 0: 0x1D meas_status .. 0x2D gas_r_lsb)
        uint8_t reg = BME690_REG_DATA;
        uint8_t data_rd[17];

        esp_err_t ret = lp_core_i2c_master_write_read_device(
            LP_I2C_NUM_0,
            BME690_I2C_ADDR,
            &reg, 1,
            data_rd, sizeof(data_rd),
            LP_I2C_TRANS_TIMEOUT_CYCLES
        );

        if (ret != ESP_OK) {
            continue;
        }

        // ✅ 4. Extract raw values (data_rd[0]=meas_status 0x1D, [1]=sub_meas_index 0x1E)
        uint32_t press_adc = ((uint32_t)data_rd[2] << 12) |
                             ((uint32_t)data_rd[3] << 4) |
                             (data_rd[4] >> 4);

        uint32_t temp_adc  = ((uint32_t)data_rd[5] << 12) |
                             ((uint32_t)data_rd[6] << 4) |
                             (data_rd[7] >> 4);

        uint32_t hum_adc   = ((uint32_t)data_rd[8] << 8) |
                              data_rd[9];

        uint16_t gas_adc   = ((uint16_t)data_rd[15] << 2) | (data_rd[16] >> 6);
        uint8_t  gas_range = data_rd[16] & 0x0F;
        bool     gas_valid = (data_rd[16] & 0x20) != 0;
        bool     heat_stab = (data_rd[16] & 0x10) != 0;

        // ✅ 5. COMPENSATION (BME690 integer formulas, BST-BME690-DS001-04)
        // T/P/H don't depend on the heater step; harmless to recompute and
        // overwrite each iteration (last step's values win).
        int64_t t_lin;
        int32_t temp_comp = calc_temp_comp(temp_adc, &t_lin);          // °C *100
        int64_t press_comp = calc_press_comp(press_adc, t_lin);        // Pa *100
        int32_t hum_comp_q = calc_hum_comp_q22_10(hum_adc, temp_comp); // Q22.10
        uint32_t gas_res_raw = calc_gas_resistance(gas_adc, gas_range);

        // ✅ 6. Return compensated values
        *temperature = (uint32_t)temp_comp;                      // °C *100
        *humidity    = (uint32_t)(((int64_t)hum_comp_q * 100) >> 10); // %RH *100
        *pressure    = (uint32_t)(press_comp / 100);              // Pa
        gas_resistance_out[step] = (gas_valid && heat_stab) ? gas_res_raw : 0; // Ohms
        gas_range_out[step] = gas_range;
        gas_valid_out[step] = gas_valid ? 1 : 0;
        heat_stab_out[step] = heat_stab ? 1 : 0;
    }
}

int main (void)
{
    temperature = 0;
    humidity = 0;
    pressure = 0;
    for (int i = 0; i < BME690_NUM_HEATER_STEPS; i++) {
        gas_resistance[i] = 0;
        gas_range[i] = 0;
        gas_valid[i] = 0;
        heat_stab[i] = 0;
    }

    bme690_soft_reset();
    bme690_read_calib();  // needed first: bme690_init() computes res_heat_x from calib data
    bme690_init();

    while (1) {
        ulp_lp_core_delay_us(5000000);  // 500ms cooldown
        bmp690_read_data(&temperature, &humidity, &pressure, gas_resistance, gas_range, gas_valid, heat_stab);
        ulp_lp_core_wakeup_main_processor();
    }
}