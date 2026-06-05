#include "ulp_lp_core_print.h"
#include "ulp_lp_core_utils.h"

int main (void)
{
    //TODO: read the data from BME690 over LP_I2C and classify it using the model 
    //ulp_lp_core_print("Hello ESP32-C6 from LP core\n");
    while (1) {
        ulp_lp_core_delay_us(5000000);  // 500ms cooldown
        ulp_lp_core_wakeup_main_processor();  
    }
}