// XL9555 IO expander (PCA9555-compatible registers) on the shared I2C bus. The LilyGo T-RGB
// routes the ST7701S config SPI, the panel power/reset and the touch reset through it; the ESP
// has no free GPIO for them (LilyGo FAQ: "T-RGB has no free GPIO and cannot be expanded").
// Bit-banged 9-bit SPI (1 D/C bit + 8 data) matches LilyGo's ExtensionIOXL9555::transfer9.
#include "xl9555.h"

#include "board_i2c.h"
#include "board_pins.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG_XL "xl9555"

// PCA9555 register map.
#define XL9555_OUTPUT0  0x02
#define XL9555_OUTPUT1  0x03
#define XL9555_CONFIG0  0x06
#define XL9555_CONFIG1  0x07

// Expander pin assignments (LilyGo-Display-IDF display_rgb.cpp).
#define XL_CS      3
#define XL_MOSI    4
#define XL_SCLK    5
#define XL_LCD_RST 6
#define XL_TP_RST  1
#define XL_POWER   2   // panel power enable, active high

static i2c_master_dev_handle_t s_dev;
static uint8_t s_out[2];            // shadow of the output registers

static i2c_master_dev_handle_t xl_dev(void)
{
    if (s_dev) return s_dev;
    i2c_master_bus_handle_t bus = board_i2c_get();
    if (!bus) return NULL;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_XL9555_ADDR,
        .scl_speed_hz = BSP_I2C_FREQ_HZ,
        .flags = {.disable_ack_check = false},
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev) == ESP_OK ? s_dev : NULL;
}

esp_err_t xl9555_init(void)
{
    i2c_master_dev_handle_t dev = xl_dev();
    if (!dev) return ESP_ERR_NOT_FOUND;
    // Every expander pin this firmware drives is an output. Two PCA9555 config-port
    // writes, each a single [reg, value] transaction: 0x00 = all pins output.
    const uint8_t cfg0[2] = {XL9555_CONFIG0, 0x00};
    const uint8_t cfg1[2] = {XL9555_CONFIG1, 0x00};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, cfg0, sizeof cfg0, -1), TAG_XL, "write cfg0");
    ESP_RETURN_ON_ERROR(i2c_master_transmit(dev, cfg1, sizeof cfg1, -1), TAG_XL, "write cfg1");
    s_out[0] = s_out[1] = 0;
    // Idle SPI clock and chip-select before the panel reset sequence.
    xl9555_write(XL_CS, 1);
    xl9555_write(XL_SCLK, 0);
    return xl9555_write(XL_POWER, 0);
}

esp_err_t xl9555_write(int pin, int level)
{
    i2c_master_dev_handle_t dev = xl_dev();
    if (!dev) return ESP_ERR_INVALID_STATE;
    const int port = pin >> 3, bit = pin & 7;
    if (level) s_out[port] |= (uint8_t)(1 << bit);
    else       s_out[port] &= (uint8_t)~(1 << bit);
    const uint8_t data[2] = {(uint8_t)(XL9555_OUTPUT0 + port), s_out[port]};
    return i2c_master_transmit(dev, data, sizeof data, -1);
}

void xl9555_lcd_reset(void)
{
    xl9555_write(XL_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    xl9555_write(XL_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void xl9555_touch_reset(void)
{
    xl9555_write(XL_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    xl9555_write(XL_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void xl9555_panel_power_on(void)
{
    xl9555_write(XL_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// 9-bit SPI: MSB first is the D/C bit (0 = command, 1 = data), then 8 payload bits. Clock idles
// low; data is sampled on the rising edge. Same wire format as LilyGo's transfer9.
void xl9555_transfer9(uint16_t data9)
{
    xl9555_write(XL_CS, 0);
    for (int i = 8; i >= 0; i--) {
        xl9555_write(XL_SCLK, 0);
        xl9555_write(XL_MOSI, (data9 >> i) & 1);
        xl9555_write(XL_SCLK, 1);
    }
    xl9555_write(XL_CS, 1);
}

void xl9555_send_cmd(uint8_t cmd)
{
    xl9555_transfer9(cmd);
}

void xl9555_send_data(const uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
        xl9555_transfer9((uint16_t)data[i] | 0x100);
}
