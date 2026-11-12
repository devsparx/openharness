// LilyGo T-RGB display bring-up — see panel_trgb.h for the provenance of every fact here.
//
// History of this bring-up (the port notes deserve the summary): LilyGo's own init table +
// timing produces a correct image on this glass; Espressif's generic esp_lcd_st7701 default
// init does NOT (the panel shows colored lines only — its interface-mode registers are for
// other ST7701 boards). Earlier text fringes tracked down to: none of MADCTL / PCLK edge /
// underruns — the residual coarseness is the TFT's pixel grid at 2.1", and phone photos of it
// add chroma aliasing of their own. Keep LilyGo's config; it is the known-good one.
#include "panel_trgb.h"

#include "board_pins.h"
#include "xl9555.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG_TRGB "panel_trgb"

// The panel is driven centered: the firmware renders the 466-wide habitat face, blitted with a
// 7px margin on this 480-wide glass (see display_habitat.c).
#define TRGB_PCLK_HZ (8 * 1000 * 1000)   // LilyGo's rate for this glass: ~29 Hz refresh

// ST7701S 2.1" init sequence (LilyGo initSequence.c, st7701_2_1_inches). `delay` is milliseconds
// after the transfer. The ED row sends 17 bytes exactly as LilyGo's (off-by-one-included) driver
// does — byte-exact replication, not correction, of the known-good stream.
typedef struct { uint8_t cmd; uint8_t data[17]; uint8_t len; uint16_t delay_ms; } trgb_cmd_t;
static const trgb_cmd_t s_init[] = {
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, {0x3b, 0x00}, 2, 0},
    {0xC1, {0x0b, 0x02}, 2, 0},
    {0xC2, {0x07, 0x02}, 2, 0},
    {0xCC, {0x10}, 1, 0},
    {0xCD, {0x08}, 1, 0},   // RGB565 mapping (666 would open with 0x00)
    {0xB0, {0x00, 0x11, 0x16, 0x0e, 0x11, 0x06, 0x05, 0x09, 0x08, 0x21, 0x06, 0x13, 0x10, 0x29, 0x31, 0x18}, 16, 0},
    {0xB1, {0x00, 0x11, 0x16, 0x0e, 0x11, 0x07, 0x05, 0x09, 0x09, 0x21, 0x05, 0x13, 0x11, 0x2a, 0x31, 0x18}, 16, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, {0x6d}, 1, 0},
    {0xB1, {0x37}, 1, 0},
    {0xB2, {0x81}, 1, 0},
    {0xB3, {0x80}, 1, 0},
    {0xB5, {0x43}, 1, 0},
    {0xB7, {0x85}, 1, 0},
    {0xB8, {0x20}, 1, 0},
    {0xC1, {0x78}, 1, 0},
    {0xC2, {0x78}, 1, 0},
    {0xC3, {0x8c}, 1, 0},
    {0xD0, {0x88}, 1, 0},
    {0xE0, {0x00, 0x00, 0x02}, 3, 0},
    {0xE1, {0x03, 0xa0, 0x00, 0x00, 0x04, 0xa0, 0x00, 0x00, 0x00, 0x20, 0x20}, 11, 0},
    {0xE2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, 0},
    {0xE3, {0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE4, {0x22, 0x00}, 2, 0},
    {0xE5, {0x05, 0xec, 0xa0, 0xa0, 0x07, 0xee, 0xa0, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xE6, {0x00, 0x00, 0x11, 0x00}, 4, 0},
    {0xE7, {0x22, 0x00}, 2, 0},
    {0xE8, {0x06, 0xed, 0xa0, 0xa0, 0x08, 0xef, 0xa0, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16, 0},
    {0xEB, {0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7, 0},
    {0xED, {0xff, 0xff, 0xff, 0xba, 0x0a, 0xbf, 0x45, 0xff, 0xff, 0x54, 0xfb, 0xa0, 0xab, 0xff, 0xff}, 16, 0},
    {0xEF, {0x10, 0x0d, 0x04, 0x08, 0x3f, 0x1f}, 6, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, {0x08}, 1, 0},
    {0xFF, {0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x36, {0x08}, 1, 0},
    {0x3A, {0x66}, 1, 0},
    {0x11, {0x00}, 0x80, 0},   // sleep out; 0x80 = delay flag in LilyGo's format
    {0x29, {0x00}, 0x80, 0},   // display on
};

// Backlight PWM on GPIO46.
#define TRGB_LEDC_TIMER LEDC_TIMER_1
#define TRGB_LEDC_MODE LEDC_LOW_SPEED_MODE
#define TRGB_LEDC_CHANNEL LEDC_CHANNEL_1

static esp_lcd_panel_handle_t s_panel;
static unsigned s_percent = 40;
static bool s_bl_on = true;

static esp_err_t trgb_backlight_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = TRGB_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = TRGB_LEDC_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG_TRGB, "timer");
    const ledc_channel_config_t channel = {
        .gpio_num = BSP_LCD_BL,
        .speed_mode = TRGB_LEDC_MODE,
        .channel = TRGB_LEDC_CHANNEL,
        .timer_sel = TRGB_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG_TRGB, "channel");
    return ledc_fade_func_install(0);
}

void trgb_set_brightness(unsigned percent)
{
    if (percent > 100) percent = 100;
    s_percent = percent;
    if (!s_bl_on) return;
    ledc_set_duty(TRGB_LEDC_MODE, TRGB_LEDC_CHANNEL, percent * 255 / 100);
    ledc_update_duty(TRGB_LEDC_MODE, TRGB_LEDC_CHANNEL);
}

void trgb_backlight(bool on)
{
    s_bl_on = on;
    ledc_set_duty(TRGB_LEDC_MODE, TRGB_LEDC_CHANNEL, on ? s_percent * 255 / 100 : 0);
    ledc_update_duty(TRGB_LEDC_MODE, TRGB_LEDC_CHANNEL);
}

esp_lcd_panel_handle_t trgb_panel_init(void)
{
    ESP_LOGI(TAG_TRGB, "ST7701S 2.1\" round, RGB565, config SPI over XL9555 (LilyGo config)");
    ESP_ERROR_CHECK(trgb_backlight_init());

    xl9555_panel_power_on();   // panel rail (expander IO2); board_detect already reset both chips
    // The wire stream below byte-for-byte replicates LilyGo's known-good init, INCLUDING its
    // do-while off-by-one: their writeData loop always emits one byte more than `len`, and that
    // trailing byte (the next table row's first data byte, or 0x00 at the end) lands in the
    // register after the params. "Fixing" the count changes the panel's register state and the
    // image breaks — measured on the bench, 2026-11-09.
    const int rows = sizeof s_init / sizeof s_init[0];
    for (int i = 0; i < rows; i++) {
        const trgb_cmd_t *c = &s_init[i];
        const int n = c->len & 0x1F;
        xl9555_send_cmd(c->cmd);
        for (int j = 0; j <= n; j++) {
            // j == n is the off-by-one byte: next row's first data byte, else 0x00.
            const uint8_t b = (j < n) ? c->data[j] : (i + 1 < rows ? s_init[i + 1].data[0] : 0x00);
            xl9555_transfer9((uint16_t)b | 0x100);
        }
        if (c->len & 0x80) vTaskDelay(pdMS_TO_TICKS(120));
        else if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    // RGB timing and the 16-line data mapping are LilyGo's verbatim (DATA0/DATA12 stay
    // unconnected on this flex; the MADCTL/color-order above makes it come out right).
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings =
            {
                .pclk_hz = TRGB_PCLK_HZ,
                .h_res = BSP_LCD_H_RES,
                .v_res = BSP_LCD_V_RES,
                .hsync_pulse_width = 1,
                .hsync_back_porch = 30,
                .hsync_front_porch = 50,
                .vsync_pulse_width = 1,
                .vsync_back_porch = 30,
                .vsync_front_porch = 20,
                .flags = {.hsync_idle_low = 0, .vsync_idle_low = 0, .de_idle_high = 0,
                          .pclk_active_neg = 1, .pclk_idle_high = 0},
            },
        .data_width = 16,
        .bits_per_pixel = 0,
        .num_fbs = 1,
        // Generous bounce buffers: the framebuffer is in PSRAM and this firmware also runs
        // code from PSRAM, so the LCD DMA needs slack to ride out cache-miss stalls without
        // underrunning (underruns read as doubled pixels and fringes on the glass).
        .bounce_buffer_size_px = 20 * BSP_LCD_H_RES,
        .psram_trans_align = 64,
        .hsync_gpio_num = BSP_LCD_HSYNC,
        .vsync_gpio_num = BSP_LCD_VSYNC,
        .de_gpio_num = BSP_LCD_DE,
        .pclk_gpio_num = BSP_LCD_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums =
            {
                BSP_LCD_DATA13, BSP_LCD_DATA14, BSP_LCD_DATA15, BSP_LCD_DATA16, BSP_LCD_DATA17,
                BSP_LCD_DATA6,  BSP_LCD_DATA7,  BSP_LCD_DATA8,  BSP_LCD_DATA9,  BSP_LCD_DATA10,
                BSP_LCD_DATA11, BSP_LCD_DATA1,  BSP_LCD_DATA2,  BSP_LCD_DATA3,  BSP_LCD_DATA4,
                BSP_LCD_DATA5,
            },
        .flags = {.disp_active_low = 0, .refresh_on_demand = 0, .fb_in_psram = 1,
                  .double_fb = 0, .no_fb = 0, .bb_invalidate_cache = 0},
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    trgb_backlight(true);
    trgb_set_brightness(s_percent);
    ESP_LOGI(TAG_TRGB, "panel up: %dx%d framebuffer in PSRAM, %lu Hz pclk",
             BSP_LCD_H_RES, BSP_LCD_V_RES, (unsigned long)TRGB_PCLK_HZ);
    return s_panel;
}
