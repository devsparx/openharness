// LilyGo T-RGB display bring-up: ST7701S over the 16-bit RGB interface, config SPI and resets
// through the XL9555 expander, backlight PWM on GPIO46. Facts and the init sequence come from
// LilyGo-Display-IDF (main/display_rgb.cpp + initSequence.c), the maintained esp-idf example for
// this panel; what follows is a C translation of that bring-up, nothing re-derived.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

// Power, reset, configure and start the RGB panel. Returns the esp_lcd panel handle the habitat
// renderer draws into. Call after board_detect() (the expander is up by then).
esp_lcd_panel_handle_t trgb_panel_init(void);
// 0..100, applied to the backlight PWM. No DCS brightness on this panel.
void trgb_set_brightness(unsigned percent);
// Visible power: the backlight. The RGB framebuffer keeps streaming; the TFT blanks with no light.
void trgb_backlight(bool on);
