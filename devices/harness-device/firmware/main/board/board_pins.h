// Which board's pin map this image is built against.
//
// THE CHOICE IS THE SoC, and that is why it is made here and not by board_detect(). The two dials
// differ in things the I2C bus can be asked about — a touch controller, a reset pin, whether a PMIC
// answers — so they share one image and settle it at boot (board.h). A different SoC cannot be asked:
// ESP32-S3 code does not run on an ESP32-P4 at all, so the panel bus, the resolution and the whole pin
// map are decided by which target the image was built for.
//
// Every file keeps including board_pins.h. Nothing includes pins_dial.h or pins_trgb.h directly.
#pragma once

#if BOARD_TRGB
// LilyGo T-RGB 2.1" round: ST7701S RGB panel via XL9555 expander, CST820/FT3267 touch, no audio.
#include "pins_trgb.h"
#else
// One board. The Pro's pin map left with its firmware.
#include "pins_dial.h"
#endif
