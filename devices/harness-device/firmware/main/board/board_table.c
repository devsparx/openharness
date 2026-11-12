// The decision table behind board_detect(): from "who answered on the bus" to pins and drivers.
// No ESP-IDF here on purpose — test/test_board.c compiles this file with a plain cc.
#include "board.h"

#include <stdbool.h>
#include <stdio.h>

static bool acked(const uint8_t *acks, int n, uint8_t addr)
{
    for (int i = 0; i < n; i++) if (acks[i] == addr) return true;
    return false;
}

// BOARD_TRGB is a compile-time switch (no sdkconfig.h here on purpose — test/test_board.c
// compiles this file with a plain cc), so in host tests this branch is inert and the dial table
// below remains what is proved.
#if BOARD_TRGB
// The T-RGB answers with a touch controller and nothing else: no PMIC, no audio codecs, and the
// XL9555 expander (0x20) is a build-time fact of this image, not a probe input. Touch: CST820
// @0x15 (2.1" full circle — driven through the CST816S esp_lcd_touch driver, same chip family)
// or FT3267 @0x38 (2.1" half circle — no driver yet, logged and run touch-less).
#define ADDR_CST820  0x15
#define ADDR_FT3267  0x38

void board_from_acks(const uint8_t *acks, int n, board_t *out)
{
    const bool cst820 = acked(acks, n, ADDR_CST820);
    const bool ft3267 = acked(acks, n, ADDR_FT3267);

    // Reset lines live on the XL9555 expander, not ESP GPIOs; both are pulsed before the probe
    // (board.c), so neither board_t pin is a real GPIO number. touch_mirror is empirical: set
    // from the first bench unit, fix if the swipe axis disagrees with the glass.
    out->lcd_rst = -1; out->touch_rst = -1;
    out->has_pmic = false;
    out->mclk_gpio = -1;
    if (cst820) {
        out->touch = TOUCH_CST820;
        out->touch_mirror = false;
        out->name = "lilygo-trgb-cst820";
    } else if (ft3267) {
        out->touch = TOUCH_NONE;      // driver lands later; everything else runs without touch
        out->touch_mirror = false;
        out->name = "lilygo-trgb-ft3267";
    } else {
        out->touch = TOUCH_NONE;
        out->touch_mirror = false;
        out->name = "lilygo-trgb";
    }
}
#else
// Measured, not assumed (2026-09-15, both units on the desk):
//   board A  ack 34 5A 18 40   (AXP2101, CST9217, ES8311, ES7210)
//   board B  ack 15 18 40      (CST816S, ES8311, ES7210)      — same before and after any reset pulse
#define ADDR_AXP2101  0x34
#define ADDR_CST9217  0x5A
#define ADDR_CST816S  0x15

void board_from_acks(const uint8_t *acks, int n, board_t *out)
{
    const bool pmic    = acked(acks, n, ADDR_AXP2101);
    const bool cst9217 = acked(acks, n, ADDR_CST9217);
    const bool cst816s = acked(acks, n, ADDR_CST816S);

    // The reset pins follow the TOUCH CHIP: the CST816S module (DXQ0175Y003) is the one wired to
    // GPIO1/2, whatever else is on the board. A dial with no touch answering at all is treated as
    // board A — the shape this code was written for, and the PMIC (if any) says so too.
    if (cst816s && !cst9217) {
        out->lcd_rst = 1;  out->touch_rst = 2;
        out->touch = TOUCH_CST816S; out->touch_mirror = false;
        out->mclk_gpio = 16;
    } else {
        out->lcd_rst = 39; out->touch_rst = 40;
        out->touch = cst9217 ? TOUCH_CST9217 : TOUCH_NONE;
        out->touch_mirror = true;
        out->mclk_gpio = 42;
    }
    out->has_pmic = pmic;
    out->name = out->touch == TOUCH_CST816S ? (pmic ? "cst816s+axp2101" : "cst816s")
              : out->touch == TOUCH_CST9217 ? (pmic ? "cst9217+axp2101" : "cst9217")
              : (pmic ? "notouch+axp2101" : "notouch");
}
#endif
