#include "board.h"
#include "board_pins.h"

#include <stdio.h>

#include "board_i2c.h"
#if BOARD_TRGB
#include "xl9555.h"
#endif
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

// Until board_detect() runs: the board this code grew up on.
#if BOARD_TRGB
static board_t s_board = {
    .name = "lilygo-trgb", .lcd_rst = -1, .touch_rst = -1,
    .touch = TOUCH_NONE, .touch_mirror = false, .has_pmic = false,
    .mclk_gpio = -1,
};
#else
static board_t s_board = {
    .name = "cst9217+axp2101", .lcd_rst = 39, .touch_rst = 40,
    .touch = TOUCH_CST9217, .touch_mirror = true, .has_pmic = true,
    .mclk_gpio = 42,
};
#endif
static bool s_detected;
static char s_describe[64];

const board_t *board(void) { return &s_board; }

static int scan(uint8_t *acks, int cap)
{
    i2c_master_bus_handle_t bus = board_i2c_get();
    if (!bus) return -1;
    int n = 0;
    // Only the addresses the decision reads, not the whole range: 3 probes at 20ms is a boot-time
    // cost nobody sees; 112 of them is not.
#if BOARD_TRGB
    static const uint8_t WANT[] = { 0x15, 0x38 };   // CST820, FT3267
#else
    static const uint8_t WANT[] = { 0x34, 0x5A, 0x15 };
#endif
    for (unsigned i = 0; i < sizeof WANT && n < cap; i++)
        if (i2c_master_probe(bus, WANT[i], 20) == ESP_OK) acks[n++] = WANT[i];
    return n;
}

// The two touch controllers' reset lines, as a pair. Measured harmless on the board that does not use
// a pair (its two pins are no-connects); the driver pulses the right one again at init anyway.
// Dial-only: the T-RGB resets touch through the XL9555 expander instead (xl9555_touch_reset).
#if !BOARD_TRGB
static void pulse(int pin)
{
    gpio_config_t io = { .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    gpio_set_level(pin, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(pin, 1); vTaskDelay(pdMS_TO_TICKS(50));
}
#endif

void board_detect(void)
{
    if (s_detected) return;
    s_detected = true;
#if BOARD_TRGB
    // The expander owns panel power, panel reset and touch reset on this board. Bring it up first,
    // power the panel, then reset both controllers before asking the bus who is there. (The dial
    // path below pulses ESP GPIOs — on the T-RGB those pins are SDMMC and touch-INT, so this
    // whole sequence is board-specific.)
    if (xl9555_init() != ESP_OK) {
        ESP_LOGE(TAG, "xl9555 missing — assuming %s touchless", s_board.name);
        board_from_acks(NULL, 0, &s_board);
        return;
    }
    xl9555_panel_power_on();
    xl9555_lcd_reset();
    xl9555_touch_reset();
#endif
    uint8_t acks[4];
    int n = scan(acks, 4);
    if (n < 0) { ESP_LOGE(TAG, "no i2c bus — assuming %s", s_board.name); return; }
#if BOARD_TRGB
    board_from_acks(acks, n, &s_board);
#else
    // Nothing that names a touch chip answered. Both are out of reset at power-on in practice (the
    // CST816S measured so; the CST9217 has always come up on the driver's own pulse), but a chip held
    // in reset by a floating line would look exactly like "no touch" — so pulse both pairs once and ask
    // again before settling for that.
    bool touch_seen = false;
    for (int i = 0; i < n; i++) touch_seen |= (acks[i] == 0x5A || acks[i] == 0x15);
    if (!touch_seen) {
        pulse(39); pulse(40); pulse(1); pulse(2);
        n = scan(acks, 4);
        if (n < 0) n = 0;
    }
    board_from_acks(acks, n, &s_board);
#endif
    char list[24]; int p = 0;
    for (int i = 0; i < n; i++) p += snprintf(list + p, sizeof list - p, "%s%02X", i ? "," : "", acks[i]);
    ESP_LOGI(TAG, "%s (ack %s)", board_describe(), n ? list : "none");
}

const char *board_describe(void)
{
    const board_t *b = &s_board;
    snprintf(s_describe, sizeof s_describe, "board %s: touch=%s pmic=%s rst=%d/%d", b->name,
             b->touch == TOUCH_CST9217 ? "cst9217" : b->touch == TOUCH_CST816S ? "cst816s"
                                     : b->touch == TOUCH_CST820 ? "cst820" : "none",
             b->has_pmic ? "yes" : "no", b->lcd_rst, b->touch_rst);
    return s_describe;
}

// BOOT (GPIO0) held at power-on. Two reads a moment apart: a single sample catches the pin mid-transition
// on a cold boot. Moved here from app_main.c when the Pro — which has no such button — joined the build.
bool board_factory_reset_requested(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BSP_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    if (gpio_get_level(BSP_BOOT_BUTTON) != 0) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
    return gpio_get_level(BSP_BOOT_BUTTON) == 0;
}
