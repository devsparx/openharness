// Pin map for the LilyGo T-RGB 2.1" round (ESP32-S3R8, 480x480 ST7701S RGB, CST820/FT3267
// touch, XL9555 IO expander, no audio codec). Reached through board_pins.h, never directly.
//
// Facts from LilyGo-Display-IDF (main/display_rgb.cpp, product_pins.h), which is the
// maintained esp-idf bring-up for this board. The ST7701S config SPI and the panel/touch
// resets live on the XL9555 expander (shared I2C), NOT on ESP GPIOs — see xl9555.c.
#pragma once

// ---- RGB panel: ST7701S, 16-bit RGB565 over an 18-line RGB666 flex ----
#define BSP_LCD_H_RES         480
#define BSP_LCD_V_RES         480
#define BSP_LCD_BIT_PER_PIXEL 16          // RGB565

#define BSP_LCD_BL           46           // backlight, PWM (sleep = off, not DCS)
#define BSP_LCD_HSYNC        47
#define BSP_LCD_VSYNC        41
#define BSP_LCD_DE           45
#define BSP_LCD_PCLK         42
// RGB565 drives the high bits of each 6-bit channel; DATA0 and DATA12 stay unconnected.
#define BSP_LCD_DATA0        44           // B0
#define BSP_LCD_DATA1        21           // B1
#define BSP_LCD_DATA2        18           // B2
#define BSP_LCD_DATA3        17           // B3
#define BSP_LCD_DATA4        16           // B4
#define BSP_LCD_DATA5        15           // B5 (unused by RGB565 mapping)
#define BSP_LCD_DATA6        14           // G0
#define BSP_LCD_DATA7        13           // G1
#define BSP_LCD_DATA8        12           // G2
#define BSP_LCD_DATA9        11           // G3
#define BSP_LCD_DATA10       10           // G4
#define BSP_LCD_DATA11       9            // G5
#define BSP_LCD_DATA12       43           // R0
#define BSP_LCD_DATA13       7            // R1
#define BSP_LCD_DATA14       6            // R2
#define BSP_LCD_DATA15       5            // R3
#define BSP_LCD_DATA16       3            // R4
#define BSP_LCD_DATA17       2            // R5

// ---- I2C bus (XL9555 expander + touch share it) ----
#define BSP_I2C_PORT         0
#define BSP_I2C_SDA          8
#define BSP_I2C_SCL          48
#define BSP_I2C_FREQ_HZ      400000

#define BSP_XL9555_ADDR      0x20          // expander, PCA9555-compatible register map

// ---- Capacitive touch: CST820 (full circle) or FT3267 (half circle), I2C ----
#define BSP_TOUCH_INT        1             // ESP GPIO; touch reset is expander IO1, not a GPIO
// Touch controller address and reset: per board — board()->touch / xl9555_touch_reset().

// ---- Power management: none (no AXP2101) ----
// Compile-time only: board()->has_pmic is always false on this board, so power.c never attaches
// a PMIC device and this address is never used. It exists so that file keeps compiling.
#define BSP_AXP2101_I2C_ADDR 0x34

// ---- Audio: none on this board; app_main and the capture/notify init paths gate on BOARD_TRGB ----

// ---- Buttons ----
#define BSP_BOOT_BUTTON      0             // hold at power-on to factory-reset pairing
