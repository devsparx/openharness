// CST820 touch driver — see touch_cst820.h for the register provenance.
#include "touch_cst820.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CST820_TAG "CST820"

// Register map (ESPHome cst816 component — one map for the CST816 family incl. CST820).
#define REG_TOUCH_NUM     0x02
#define REG_XPOS_HIGH     0x03
#define REG_XPOS_LOW      0x04
#define REG_YPOS_HIGH     0x05
#define REG_YPOS_LOW      0x06
#define REG_DIS_AUTOSLEEP 0xFE
#define REG_CHIP_ID       0xA7
#define REG_IRQ_CTL       0xFA
#define IRQ_EN_MOTION     0x70

#define CST820_CHIP_ID    0xB7
#define CST816S_CHIP_ID   0xB4

static esp_err_t cst820_read_data(esp_lcd_touch_handle_t tp);
static bool cst820_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                          uint8_t *point_num, uint8_t max_point_num);
static esp_err_t cst820_del(esp_lcd_touch_handle_t tp);
static esp_err_t cst820_reset(esp_lcd_touch_handle_t tp);
static esp_err_t cst820_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len);
static esp_err_t cst820_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t value);

esp_err_t esp_lcd_touch_new_i2c_cst820(const esp_lcd_panel_io_handle_t io,
                                       const esp_lcd_touch_config_t *config,
                                       esp_lcd_touch_handle_t *tp)
{
    ESP_RETURN_ON_FALSE(io && config && tp, ESP_ERR_INVALID_ARG, CST820_TAG, "argument is NULL");

    esp_err_t ret = ESP_OK;   // ESP_GOTO_ON_* assign this on the error path
    esp_lcd_touch_handle_t cst820 = calloc(1, sizeof(esp_lcd_touch_t));
    ESP_GOTO_ON_FALSE(cst820, ESP_ERR_NO_MEM, err, CST820_TAG, "Touch handle malloc failed");

    cst820->io = io;
    cst820->read_data = cst820_read_data;
    cst820->get_xy = cst820_get_xy;
    cst820->del = cst820_del;
    cst820->data.lock.owner = portMUX_FREE_VAL;
    memcpy(&cst820->config, config, sizeof(esp_lcd_touch_config_t));

    if (cst820->config.int_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t int_gpio_config = {
            .mode = GPIO_MODE_INPUT,
            .intr_type = (cst820->config.levels.interrupt ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE),
            .pin_bit_mask = BIT64(cst820->config.int_gpio_num),
        };
        ESP_GOTO_ON_ERROR(gpio_config(&int_gpio_config), err, CST820_TAG, "GPIO intr config failed");
        if (cst820->config.interrupt_callback)
            esp_lcd_touch_register_interrupt_callback(cst820, cst820->config.interrupt_callback);
    }
    if (cst820->config.rst_gpio_num != GPIO_NUM_NC) {
        const gpio_config_t rst_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = BIT64(cst820->config.rst_gpio_num),
        };
        ESP_GOTO_ON_ERROR(gpio_config(&rst_gpio_config), err, CST820_TAG, "GPIO reset config failed");
    }
    ESP_GOTO_ON_ERROR(cst820_reset(cst820), err, CST820_TAG, "Reset failed");

    // Keep the chip awake BEFORE asking who it is: a sleeping CST820 NAKs the ID read, which is
    // exactly how the stock CST816S driver fails on this hardware.
    ESP_GOTO_ON_ERROR(cst820_i2c_write(cst820, REG_IRQ_CTL, IRQ_EN_MOTION), err, CST820_TAG,
                      "IRQ ctl write failed");
    ESP_GOTO_ON_ERROR(cst820_i2c_write(cst820, REG_DIS_AUTOSLEEP, 0x01), err, CST820_TAG,
                      "autosleep write failed");

    uint8_t id = 0;
    esp_err_t rc = ESP_FAIL;
    for (int attempt = 0; attempt < 5 && rc != ESP_OK; attempt++) {
        rc = cst820_i2c_read(cst820, REG_CHIP_ID, &id, 1);
        if (rc != ESP_OK) vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_GOTO_ON_ERROR(rc, err, CST820_TAG, "chip ID read failed (chip asleep?)");
    ESP_LOGI(CST820_TAG, "IC id: 0x%02X (%s)", id, id == CST820_CHIP_ID ? "CST820"
                                                       : id == CST816S_CHIP_ID ? "CST816S" : "CST816 family");

    *tp = cst820;
    return ESP_OK;
err:
    if (cst820) cst820_del(cst820);
    ESP_LOGE(CST820_TAG, "Initialization failed!");
    return ret;
}

static esp_err_t cst820_read_data(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, CST820_TAG, "Touch controller handle is NULL");

    // One point is all the dial reads; the family's window is read from 0x00 with the count at
    // offset 2 (ESPHome reads 13 bytes for gesture data; the dial ignores gestures).
    uint8_t data[7] = {0};
    ESP_RETURN_ON_ERROR(cst820_i2c_read(tp, 0x00, data, sizeof data), CST820_TAG, "I2C read failed");

    const uint8_t num = data[REG_TOUCH_NUM] & 3;
    portENTER_CRITICAL(&tp->data.lock);
    tp->data.points = num;
    if (num > 0) {
        tp->data.coords[0].x = (uint16_t)((data[REG_XPOS_HIGH] & 0x0F) << 8) | data[REG_XPOS_LOW];
        tp->data.coords[0].y = (uint16_t)((data[REG_YPOS_HIGH] & 0x0F) << 8) | data[REG_YPOS_LOW];
        tp->data.coords[0].strength = 1;
    }
    portEXIT_CRITICAL(&tp->data.lock);
    return ESP_OK;
}

static bool cst820_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength,
                          uint8_t *point_num, uint8_t max_point_num)
{
    ESP_RETURN_ON_FALSE(tp && x && y && point_num, false, CST820_TAG, "argument is NULL");
    ESP_RETURN_ON_FALSE(max_point_num > 0, false, CST820_TAG, "max_point_num must be >= 1");

    portENTER_CRITICAL(&tp->data.lock);
    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);
    for (size_t i = 0; i < *point_num; i++) {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;
        if (strength) strength[i] = tp->data.coords[i].strength;
    }
    tp->data.points = 0;   // consumed
    portEXIT_CRITICAL(&tp->data.lock);
    return *point_num > 0;
}

static esp_err_t cst820_del(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, CST820_TAG, "Touch controller handle is NULL");
    if (tp->config.int_gpio_num != GPIO_NUM_NC) {
        gpio_reset_pin(tp->config.int_gpio_num);
        if (tp->config.interrupt_callback) gpio_isr_handler_remove(tp->config.int_gpio_num);
    }
    if (tp->config.rst_gpio_num != GPIO_NUM_NC) gpio_reset_pin(tp->config.rst_gpio_num);
    free(tp);
    return ESP_OK;
}

static esp_err_t cst820_reset(esp_lcd_touch_handle_t tp)
{
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, CST820_TAG, "Touch controller handle is NULL");
    if (tp->config.rst_gpio_num == GPIO_NUM_NC) return ESP_OK;   // expander reset ran in board_detect
    ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), CST820_TAG,
                        "GPIO set level failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), CST820_TAG,
                        "GPIO set level failed");
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

static esp_err_t cst820_i2c_read(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t *data, uint8_t len)
{
    ESP_RETURN_ON_FALSE(tp && data, ESP_ERR_INVALID_ARG, CST820_TAG, "argument is NULL");
    return esp_lcd_panel_io_rx_param(tp->io, reg, data, len);
}

static esp_err_t cst820_i2c_write(esp_lcd_touch_handle_t tp, uint16_t reg, uint8_t value)
{
    ESP_RETURN_ON_FALSE(tp, ESP_ERR_INVALID_ARG, CST820_TAG, "argument is NULL");
    return esp_lcd_panel_io_tx_param(tp->io, reg, &value, 1);
}
