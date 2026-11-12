// XL9555 IO expander — see xl9555.c for the register map and pin assignments.
#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t xl9555_init(void);            // configure expander ports as outputs
esp_err_t xl9555_write(int pin, int level);  // expander GPIO write (internal pins, see xl9555.c)
void xl9555_lcd_reset(void);            // pulse the ST7701S reset (expander IO6)
void xl9555_touch_reset(void);          // pulse the touch reset (expander IO1)
void xl9555_panel_power_on(void);       // ST7701S power enable (expander IO2)
void xl9555_transfer9(uint16_t data9);  // raw 9-bit SPI word (D/C bit + 8 data)
void xl9555_send_cmd(uint8_t cmd);      // 9-bit SPI: D/C=0
void xl9555_send_data(const uint8_t *data, int len);  // 9-bit SPI: D/C=1
