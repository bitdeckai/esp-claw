/*
 * Kode Dot board-specific factory entries for ESP Board Manager.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst820.h"

static const char *TAG = "KODE_DOT_SETUP_DEVICE";

#define KODE_DOT_BACKLIGHT_GPIO   22

static void __attribute__((constructor)) kode_dot_early_init(void)
{
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = BIT64(KODE_DOT_BACKLIGHT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    if (gpio_config(&bl_cfg) == ESP_OK) {
        gpio_set_level(KODE_DOT_BACKLIGHT_GPIO, 1);
    }
}

esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle, const uint16_t dev_addr, esp_io_expander_handle_t *handle_ret)
{
    esp_err_t ret = esp_io_expander_new_i2c_tca95xx_16bit(i2c_handle, dev_addr, handle_ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TCA95xx IO expander handle: %s", esp_err_to_name(ret));
        return ret;
    }

    /* EXP3: amplifier SD (off), EXP4: 3V3 peripherals enable (on). */
    const uint32_t output_mask = IO_EXPANDER_PIN_NUM_3 | IO_EXPANDER_PIN_NUM_4;
    ret = esp_io_expander_set_dir(*handle_ret, output_mask, IO_EXPANDER_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set expander output direction: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_io_expander_set_level(*handle_ret, IO_EXPANDER_PIN_NUM_3, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable amplifier by default: %s", esp_err_to_name(ret));
    }

    ret = esp_io_expander_set_level(*handle_ret, IO_EXPANDER_PIN_NUM_4, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable 3V3 peripheral rail: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = esp_lcd_new_panel_co5300(io, panel_dev_config, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CO5300 panel handle: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *touch_dev_config, esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = esp_lcd_touch_new_i2c_cst820(io, touch_dev_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CST820 touch handle: %s", esp_err_to_name(ret));
    }
    return ret;
}
