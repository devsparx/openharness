// CST820 capacitive touch controller as an esp_lcd_touch driver — for the LilyGo T-RGB 2.1"
// full-circle dial. The chip is register-compatible with the CST816 family the Espressif
// driver speaks (ESPHome's cst816 component covers CST816S/T/D, CST820 and CST826 with one
// register map), with two differences that matter here:
//
//   * It auto-sleeps within seconds of idle and NAKs I2C while asleep — so init writes the
//     motion-IRQ and disable-autosleep registers BEFORE reading the chip ID, and read_id
//     retries instead of failing once. (LilyGo's driver calls disableAutoSleep() for the same
//     reason; ESPHome enables the motion IRQ.)
//   * The chip ID at 0xA7 is 0xB7 (CST816S is 0xB4). Any family ID is accepted; the value is
//     logged for the bring-up notes.
//
// Register map from ESPHome (components/cst816), used with CST820 on the ESP32-2432S024C and
// LilyGo boards in the field; reading 7 bytes from 0x00 matches the family's data window.
#pragma once

#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io,
                                       const esp_lcd_touch_config_t *config,
                                       esp_lcd_touch_handle_t *tp);

#ifdef __cplusplus
}
#endif
