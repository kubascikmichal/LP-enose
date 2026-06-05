#include "ulp_lp_core_print.h"
#include "ulp_lp_core_utils.h"
#include "ulp_lp_core_i2c.h"

#define BME690_I2C_ADDR 0x76  // or 0x77
#define BME690_REG_DATA 0x1D

#define LP_I2C_TRANS_TIMEOUT_CYCLES 5000

volatile uint32_t temperature;
volatile uint16_t humidity;
volatile uint32_t pressure;

static void bme690_init()
{
    uint8_t cmd[2];

    // ✅ 1. Soft reset
    cmd[0] = 0xE0;   // reset register
    cmd[1] = 0xB6;
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );

    ulp_lp_core_delay_us(5000);  // 5 ms


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


    // ✅ 5. Enable gas + heater (like s_heatr_conf)
    cmd[0] = 0x71;
    cmd[1] = 0x10;  // run_gas = 1
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );


    // ✅ 6. Heater temperature (~300°C)
    cmd[0] = 0x5A;
    cmd[1] = 0x73;  // approx (matches Bosch calc loosely)
    lp_core_i2c_master_write_to_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        cmd, 2,
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );


    // ✅ 7. Heater duration (~100 ms)
    cmd[0] = 0x64;
    cmd[1] = 0x59;
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


void bmp690_read_data(uint32_t *temperature, uint16_t *humidity, uint32_t *pressure)
{
    bme690_trigger_measurement();
    ulp_lp_core_delay_us(10000);  // wait for measurement to complete (max time is ~10ms 

    uint8_t reg = BME690_REG_DATA;
    uint8_t data_rd[8];

    esp_err_t ret = lp_core_i2c_master_write_read_device(
        LP_I2C_NUM_0,
        BME690_I2C_ADDR,
        &reg, 1,               // write register address
        data_rd, sizeof(data_rd),
        LP_I2C_TRANS_TIMEOUT_CYCLES
    );

    if (ret != ESP_OK) {
        return;
    }

    // Raw values (NOT compensated!)
    uint32_t press_raw = ((uint32_t)data_rd[0] << 12) |
                         ((uint32_t)data_rd[1] << 4) |
                         (data_rd[2] >> 4);

    uint32_t temp_raw  = ((uint32_t)data_rd[3] << 12) |
                         ((uint32_t)data_rd[4] << 4) |
                         (data_rd[5] >> 4);

    uint16_t hum_raw   = ((uint16_t)data_rd[6] << 8) |
                          data_rd[7];

    // For demonstration, we return raw values. In a real application, you would apply compensation here.
    *temperature = temp_raw;
    *humidity = hum_raw;
    *pressure = press_raw;

}

int main (void)
{
    temperature = 0;
    humidity = 0;
    pressure = 0;
    //TODO: read the data from BME690 over LP_I2C and classify it using the model 
    //ulp_lp_core_print("Hello ESP32-C6 from LP core\n");
    bme690_init();
    while (1) {
        ulp_lp_core_delay_us(5000000);  // 500ms cooldown
        bmp690_read_data((uint32_t*) &temperature, &humidity, &pressure);
        ulp_lp_core_wakeup_main_processor();  
    }
}