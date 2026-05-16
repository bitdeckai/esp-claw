/*
 * Kode Dot board-specific factory entries for ESP Board Manager.
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_board_entry.h"
#include "esp_board_periph.h"
#include "gen_board_device_custom.h"

static const char *TAG = "KODE_DOT_SETUP_DEVICE";

#define KODE_DOT_BACKLIGHT_GPIO   22
#define KODE_DOT_LCD_X_GAP        22
#define KODE_DOT_LCD_Y_GAP        0

#define BQ25896_REG_CHG_OTG_CFG   0x03
#define BQ25896_REG_STATUS         0x0B
#define BQ25896_OTG_CONFIG_MASK    BIT(5)
#define BQ25896_VBUS_STAT_MASK     0xE0

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    const char *peripheral_name;
    TaskHandle_t monitor_task;
    int8_t otg_enabled;
    bool stop_task;
} kode_dot_pmic_handle_t;

static esp_err_t bq25896_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(dev, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

static esp_err_t bq25896_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    const uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t bq25896_update_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t cur = 0;
    esp_err_t ret = bq25896_read_reg(dev, reg, &cur);
    if (ret != ESP_OK) {
        return ret;
    }
    uint8_t next = (cur & ~mask) | (value & mask);
    if (next == cur) {
        return ESP_OK;
    }
    return bq25896_write_reg(dev, reg, next);
}

static esp_err_t bq25896_set_otg_enable(i2c_master_dev_handle_t dev, bool enable)
{
    return bq25896_update_bits(dev,
                               BQ25896_REG_CHG_OTG_CFG,
                               BQ25896_OTG_CONFIG_MASK,
                               enable ? BQ25896_OTG_CONFIG_MASK : 0);
}

static esp_err_t bq25896_apply_otg_policy(kode_dot_pmic_handle_t *handle)
{
    uint8_t status = 0;
    esp_err_t ret = bq25896_read_reg(handle->dev, BQ25896_REG_STATUS, &status);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Read BQ25896 REG0B failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uint8_t vbus_stat = (status & BQ25896_VBUS_STAT_MASK) >> 5;
    /* REG0B[7:5]: 000 = no input, 111 = OTG mode, 001..110 = external input present. */
    bool input_present = (vbus_stat >= 1) && (vbus_stat <= 6);
    bool want_otg = !input_present;
    ret = bq25896_update_bits(handle->dev,
                              BQ25896_REG_CHG_OTG_CFG,
                              BQ25896_OTG_CONFIG_MASK,
                              want_otg ? BQ25896_OTG_CONFIG_MASK : 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Update BQ25896 OTG_CONFIG failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (handle->otg_enabled != (int8_t)want_otg) {
        handle->otg_enabled = (int8_t)want_otg;
        ESP_LOGI(TAG,
                 "BQ25896 policy: input_%s, OTG_%s",
                 input_present ? "present" : "absent",
                 want_otg ? "enabled" : "disabled");
    }
    return ESP_OK;
}

static void kode_dot_pmic_monitor_task(void *arg)
{
    kode_dot_pmic_handle_t *handle = (kode_dot_pmic_handle_t *)arg;
    while (!handle->stop_task) {
        bq25896_apply_otg_policy(handle);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelete(NULL);
}

static int pmic_init(void *config, int cfg_size, void **device_handle)
{
    if (config == NULL || device_handle == NULL || cfg_size != sizeof(dev_custom_pmic_config_t)) {
        return ESP_ERR_INVALID_ARG;
    }

    const dev_custom_pmic_config_t *cfg = (const dev_custom_pmic_config_t *)config;
    if (cfg->chip == NULL || strcmp(cfg->chip, "bq25896") != 0) {
        ESP_LOGE(TAG, "Unsupported PMIC chip: %s", cfg->chip ? cfg->chip : "(null)");
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t ret = esp_board_periph_get_handle(cfg->peripheral_name, (void **)&bus);
    if (ret != ESP_OK || bus == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C peripheral '%s': %s", cfg->peripheral_name, esp_err_to_name(ret));
        return ret != ESP_OK ? ret : ESP_FAIL;
    }

    kode_dot_pmic_handle_t *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }

    handle->bus = bus;
    handle->peripheral_name = cfg->peripheral_name;
    handle->otg_enabled = -1;

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = (uint16_t)cfg->i2c_addr,
        .scl_speed_hz = (uint32_t)cfg->frequency,
    };
    ret = i2c_master_bus_add_device(bus, &dev_cfg, &handle->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Add BQ25896 to I2C bus failed: %s", esp_err_to_name(ret));
        free(handle);
        return ret;
    }

    ret = bq25896_set_otg_enable(handle->dev, true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Force enable OTG at boot failed: %s", esp_err_to_name(ret));
    }

    bq25896_apply_otg_policy(handle);

    BaseType_t task_ok = xTaskCreate(kode_dot_pmic_monitor_task,
                                     "bq25896_mon",
                                     3072,
                                     handle,
                                     4,
                                     &handle->monitor_task);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "Create BQ25896 monitor task failed");
        i2c_master_bus_rm_device(handle->dev);
        free(handle);
        return ESP_FAIL;
    }

    *device_handle = handle;
    return ESP_OK;
}

static int pmic_deinit(void *device_handle)
{
    kode_dot_pmic_handle_t *handle = (kode_dot_pmic_handle_t *)device_handle;
    if (handle == NULL) {
        return ESP_OK;
    }

    if (handle->monitor_task != NULL) {
        handle->stop_task = true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (handle->dev != NULL) {
        i2c_master_bus_rm_device(handle->dev);
    }
    if (handle->peripheral_name != NULL) {
        esp_board_periph_unref_handle(handle->peripheral_name);
    }
    free(handle);
    return ESP_OK;
}

/* Align CO5300 power-up sequence with official setup flow. */
static const co5300_lcd_init_cmd_t kode_dot_co5300_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},

    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static const co5300_vendor_config_t kode_dot_co5300_vendor_config = {
    .init_cmds = kode_dot_co5300_init_cmds,
    .init_cmds_size = sizeof(kode_dot_co5300_init_cmds) / sizeof(kode_dot_co5300_init_cmds[0]),
};

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

    /* EXP3: amplifier SD (on), EXP4: 3V3 peripherals enable (on). */
    const uint32_t output_mask = IO_EXPANDER_PIN_NUM_3 | IO_EXPANDER_PIN_NUM_4;
    ret = esp_io_expander_set_dir(*handle_ret, output_mask, IO_EXPANDER_OUTPUT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set expander output direction: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_io_expander_set_level(*handle_ret, IO_EXPANDER_PIN_NUM_3, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable amplifier by default: %s", esp_err_to_name(ret));
    }

    ret = esp_io_expander_set_level(*handle_ret, IO_EXPANDER_PIN_NUM_4, 1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable 3V3 peripheral rail: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(panel_dev_cfg));
    panel_dev_cfg.vendor_config = (void *)&kode_dot_co5300_vendor_config;

    esp_err_t ret = esp_lcd_new_panel_co5300(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CO5300 panel handle: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_lcd_panel_set_gap(*ret_panel, KODE_DOT_LCD_X_GAP, KODE_DOT_LCD_Y_GAP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CO5300 panel gap (%d, %d): %s",
                 KODE_DOT_LCD_X_GAP,
                 KODE_DOT_LCD_Y_GAP,
                 esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t lcd_touch_factory_entry_t(esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *touch_dev_config, esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = esp_lcd_touch_new_i2c_cst820(io, touch_dev_config, ret_touch);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CST820 touch handle: %s", esp_err_to_name(ret));
    }
    return ret;
}

CUSTOM_DEVICE_IMPLEMENT(pmic, pmic_init, pmic_deinit);
