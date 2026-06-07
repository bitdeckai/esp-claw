/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "emote.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_board_manager_includes.h"
#include "expression_emote.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "gfx.h"
#include "display_arbiter.h"

static const char *TAG = "app_emote";

#define EMOTE_ASSETS_PARTITION "emote"
#define EMOTE_MIN_VALID_EPOCH 1700000000
#define EMOTE_LLM_COUNT 4
#define EMOTE_IM_COUNT 4
#define EMOTE_PROVIDER_ICON_SIZE 48
#define EMOTE_LLM_EXTRA_UP 78
#define EMOTE_CENTER_OCCUPY_Y_OFFSET 20
#define EMOTE_BADGE_ANIM_PERIOD_MS 180
#define EMOTE_BADGE_ACTIVE_MS 2500
#define EMOTE_BADGE_ANIM_TASK_STACK (6 * 1024)
#define EMOTE_IM_BOUNCE_PIXELS 3
#define EMOTE_TIME_UPDATE_MS 1000
#define EMOTE_IM_BOUNCE_STEP_MS 150
#define EMOTE_IM_BOUNCE_STEPS 6

#define EMOTE_COLOR_DIM_HEX      0x7A7A7A
#define EMOTE_COLOR_LLM_ON_HEX   0x4DD488
#define EMOTE_COLOR_IM_ON_HEX    0x63B6FF
#define EMOTE_COLOR_IM_ACTIVE_HEX 0xFFE08A

typedef enum {
    EMOTE_LLM_OPENAI = 0,
    EMOTE_LLM_BAILIAN,
    EMOTE_LLM_DEEPSEEK,
    EMOTE_LLM_ANTHROPIC,
} emote_llm_provider_t;

typedef enum {
    EMOTE_IM_WECHAT = 0,
    EMOTE_IM_QQ,
    EMOTE_IM_FEISHU,
    EMOTE_IM_TELEGRAM,
} emote_im_provider_t;

typedef struct {
    bool llm_configured[EMOTE_LLM_COUNT];
    bool im_configured[EMOTE_IM_COUNT];
    int active_im_index;
    int64_t active_im_mark_ms;
    bool created;
    gfx_obj_t *llm_labels[EMOTE_LLM_COUNT];
    gfx_obj_t *im_labels[EMOTE_IM_COUNT];
    gfx_obj_t *center_logo;
    gfx_obj_t *clock_label;
    gfx_obj_t *llm_icons[EMOTE_LLM_COUNT];
    gfx_obj_t *im_icons[EMOTE_IM_COUNT];
    gfx_image_dsc_t center_logo_dsc;
    gfx_image_dsc_t llm_icon_dsc[EMOTE_LLM_COUNT];
    gfx_image_dsc_t im_icon_dsc[EMOTE_IM_COUNT];
    void *center_logo_cache;
    void *llm_icon_cache[EMOTE_LLM_COUNT];
    void *im_icon_cache[EMOTE_IM_COUNT];
    bool llm_icon_ready[EMOTE_LLM_COUNT];
    bool im_icon_ready[EMOTE_IM_COUNT];
    bool llm_icon_loaded_configured[EMOTE_LLM_COUNT];
    bool im_icon_loaded_configured[EMOTE_IM_COUNT];
    int im_anim_phase;
    char clock_text_cache[16];
    bool clock_text_cached;
    gfx_coord_t llm_base_x[EMOTE_LLM_COUNT];
    gfx_coord_t llm_base_y[EMOTE_LLM_COUNT];
    gfx_coord_t im_base_x[EMOTE_IM_COUNT];
    gfx_coord_t im_base_y[EMOTE_IM_COUNT];
} emote_provider_badges_t;

static const char *s_llm_logo_color[EMOTE_LLM_COUNT] = {
    "l_oa_c",
    "l_bl_c",
    "l_ds_c",
    "l_an_c",
};

static const char *s_llm_logo_gray[EMOTE_LLM_COUNT] = {
    "l_oa_g",
    "l_bl_g",
    "l_ds_g",
    "l_an_g",
};

static const char *s_im_logo_color[EMOTE_IM_COUNT] = {
    "i_wc_c",
    "i_qq_c",
    "i_fs_c",
    "i_tg_c",
};

static const char *s_im_logo_gray[EMOTE_IM_COUNT] = {
    "i_wc_g",
    "i_qq_g",
    "i_fs_g",
    "i_tg_g",
};

static const char *s_center_logo = "c_claw";

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;
static int s_lcd_width;
static int s_lcd_height;
static emote_handle_t s_emote_handle;
static TaskHandle_t s_badge_anim_task;
static emote_provider_badges_t s_provider_badges = {
    .active_im_index = -1,
    .active_im_mark_ms = 0,
    .im_anim_phase = 0,
};

extern const void *emote_acquire_data(emote_handle_t handle, const void *data_ref, size_t size, void **output_ptr);

static bool emote_str_has_text(const char *value)
{
    return value != NULL && value[0] != '\0';
}

static bool emote_contains_ci(const char *haystack, const char *needle)
{
    size_t needle_len;
    const char *h;

    if (!haystack || !needle) {
        return false;
    }

    needle_len = strlen(needle);
    if (needle_len == 0) {
        return true;
    }

    for (h = haystack; *h; ++h) {
        size_t i = 0;
        while (i < needle_len && h[i] &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return true;
        }
    }

    return false;
}

static int emote_detect_llm_provider(const char *llm_backend_type,
                                     const char *llm_base_url,
                                     const char *llm_model,
                                     const char *llm_api_key)
{
    if (!emote_str_has_text(llm_api_key)) {
        return -1;
    }

    if (emote_contains_ci(llm_base_url, "dashscope") ||
            emote_contains_ci(llm_base_url, "aliyun") ||
            emote_contains_ci(llm_base_url, "bailian") ||
            emote_contains_ci(llm_model, "qwen")) {
        return EMOTE_LLM_BAILIAN;
    }

    if (emote_contains_ci(llm_base_url, "deepseek") ||
            emote_contains_ci(llm_model, "deepseek")) {
        return EMOTE_LLM_DEEPSEEK;
    }

    if (emote_contains_ci(llm_backend_type, "anthropic") ||
            emote_contains_ci(llm_base_url, "anthropic") ||
            emote_contains_ci(llm_model, "claude")) {
        return EMOTE_LLM_ANTHROPIC;
    }

    if (emote_contains_ci(llm_backend_type, "openai") ||
            emote_contains_ci(llm_base_url, "openai") ||
            emote_contains_ci(llm_model, "gpt")) {
        return EMOTE_LLM_OPENAI;
    }

    return EMOTE_LLM_OPENAI;
}

static int emote_im_platform_index(const char *platform)
{
    if (!emote_str_has_text(platform)) {
        return -1;
    }

    if (emote_contains_ci(platform, "wechat") || emote_contains_ci(platform, "wx")) {
        return EMOTE_IM_WECHAT;
    }
    if (emote_contains_ci(platform, "qq")) {
        return EMOTE_IM_QQ;
    }
    if (emote_contains_ci(platform, "feishu") || emote_contains_ci(platform, "lark")) {
        return EMOTE_IM_FEISHU;
    }
    if (emote_contains_ci(platform, "telegram") || emote_contains_ci(platform, "tg")) {
        return EMOTE_IM_TELEGRAM;
    }

    return -1;
}

static bool emote_load_logo_to_dsc(const char *icon_name, gfx_image_dsc_t *out_dsc)
{
    icon_data_t *icon = NULL;
    const void *src_data = NULL;
    void **cache_ptr = NULL;
    int idx = -1;
    bool is_llm = false;
    int i;

    if (!s_emote_handle || !icon_name || !out_dsc) {
        return false;
    }

    if (emote_get_icon_data_by_name(s_emote_handle, icon_name, &icon) != ESP_OK ||
            !icon || !icon->data || icon->size <= sizeof(gfx_image_header_t)) {
        return false;
    }

    for (i = 0; i < EMOTE_LLM_COUNT; ++i) {
        if (strcmp(icon_name, s_llm_logo_color[i]) == 0 || strcmp(icon_name, s_llm_logo_gray[i]) == 0) {
            idx = i;
            is_llm = true;
            break;
        }
    }
    if (idx < 0) {
        for (i = 0; i < EMOTE_IM_COUNT; ++i) {
            if (strcmp(icon_name, s_im_logo_color[i]) == 0 || strcmp(icon_name, s_im_logo_gray[i]) == 0) {
                idx = i;
                is_llm = false;
                break;
            }
        }
    }

    if (idx >= 0) {
        cache_ptr = is_llm ? &s_provider_badges.llm_icon_cache[idx] : &s_provider_badges.im_icon_cache[idx];
    } else if (strcmp(icon_name, s_center_logo) == 0) {
        cache_ptr = &s_provider_badges.center_logo_cache;
    } else {
        return false;
    }

    src_data = emote_acquire_data(s_emote_handle, icon->data, icon->size, cache_ptr);
    if (!src_data) {
        return false;
    }

    memcpy(&out_dsc->header, src_data, sizeof(gfx_image_header_t));
    out_dsc->data = (const uint8_t *)src_data + sizeof(gfx_image_header_t);
    out_dsc->data_size = icon->size - sizeof(gfx_image_header_t);
    out_dsc->reserved = NULL;
    out_dsc->reserved_2 = NULL;
    return true;
}

static void emote_update_time_label_locked(void)
{
    char clock_text[16] = "--:--:--";
    time_t now;
    struct tm local_tm = {0};
    uint16_t w_clock = 0;
    uint16_t h_clock = 0;
    gfx_coord_t x_clock;
    gfx_coord_t y_clock;
    const int bottom_pad = 3;

    if (!s_provider_badges.clock_label) {
        return;
    }

    now = time(NULL);
    if (now >= EMOTE_MIN_VALID_EPOCH && localtime_r(&now, &local_tm) != NULL) {
        strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &local_tm);
    }

    if (!s_provider_badges.clock_text_cached || strcmp(s_provider_badges.clock_text_cache, clock_text) != 0) {
        gfx_label_set_text(s_provider_badges.clock_label, clock_text);
        strncpy(s_provider_badges.clock_text_cache, clock_text, sizeof(s_provider_badges.clock_text_cache) - 1);
        s_provider_badges.clock_text_cache[sizeof(s_provider_badges.clock_text_cache) - 1] = '\0';
        s_provider_badges.clock_text_cached = true;
    }

    gfx_obj_get_size(s_provider_badges.clock_label, &w_clock, &h_clock);

    y_clock = (gfx_coord_t)(s_lcd_height - (int)h_clock - bottom_pad);
    x_clock = (gfx_coord_t)((s_lcd_width - (int)w_clock) / 2);

    gfx_obj_set_pos(s_provider_badges.clock_label, x_clock, y_clock);
    gfx_label_set_color(s_provider_badges.clock_label, GFX_COLOR_HEX(0xF2F2F2));
    gfx_label_set_bg_enable(s_provider_badges.clock_label, false);
    gfx_obj_set_visible(s_provider_badges.clock_label, true);
}

static void emote_update_provider_badges_locked(void)
{
    static const uint32_t k_dim = EMOTE_COLOR_DIM_HEX;
    static const uint32_t k_llm_on = EMOTE_COLOR_LLM_ON_HEX;
    static const uint32_t k_im_on = EMOTE_COLOR_IM_ON_HEX;
    static const uint32_t k_im_active = EMOTE_COLOR_IM_ACTIVE_HEX;
    int i;
    int64_t now_ms = esp_timer_get_time() / 1000;
    int bounce_y_ofs = 0;

    if (!s_provider_badges.created) {
        return;
    }

    emote_update_time_label_locked();

    if (s_provider_badges.active_im_index >= 0 &&
            (now_ms - s_provider_badges.active_im_mark_ms) < 2500) {
        int64_t elapsed_ms = now_ms - s_provider_badges.active_im_mark_ms;
        int64_t bounce_window_ms = (int64_t)EMOTE_IM_BOUNCE_STEP_MS * EMOTE_IM_BOUNCE_STEPS;
        if (elapsed_ms < bounce_window_ms) {
            int step = (int)(elapsed_ms / EMOTE_IM_BOUNCE_STEP_MS);
            bounce_y_ofs = (step % 2 == 0) ? -EMOTE_IM_BOUNCE_PIXELS : EMOTE_IM_BOUNCE_PIXELS;
        }
    }

    if (s_provider_badges.center_logo) {
        // Keep the default emote animation visible in the center and avoid an extra overlay image.
        gfx_obj_set_visible(s_provider_badges.center_logo, false);
    }

    for (i = 0; i < EMOTE_LLM_COUNT; ++i) {
        const bool configured = s_provider_badges.llm_configured[i];
        const char *icon_name = configured ? s_llm_logo_color[i] : s_llm_logo_gray[i];

        if (s_provider_badges.llm_icons[i] &&
                (!s_provider_badges.llm_icon_ready[i] ||
                 s_provider_badges.llm_icon_loaded_configured[i] != configured) &&
                emote_load_logo_to_dsc(icon_name, &s_provider_badges.llm_icon_dsc[i])) {
            gfx_img_set_src(s_provider_badges.llm_icons[i], &s_provider_badges.llm_icon_dsc[i]);
            s_provider_badges.llm_icon_loaded_configured[i] = configured;
            s_provider_badges.llm_icon_ready[i] = true;
        }

        if (s_provider_badges.llm_icons[i] && s_provider_badges.llm_icon_ready[i]) {
            gfx_obj_set_pos(s_provider_badges.llm_icons[i], s_provider_badges.llm_base_x[i], s_provider_badges.llm_base_y[i]);
            gfx_obj_set_visible(s_provider_badges.llm_icons[i], true);
            if (s_provider_badges.llm_labels[i]) {
                gfx_obj_set_visible(s_provider_badges.llm_labels[i], false);
            }
            continue;
        }

        s_provider_badges.llm_icon_ready[i] = false;

        if (s_provider_badges.llm_labels[i]) {
            gfx_label_set_color(s_provider_badges.llm_labels[i], GFX_COLOR_HEX(configured ? k_llm_on : k_dim));
            gfx_obj_set_pos(s_provider_badges.llm_labels[i], s_provider_badges.llm_base_x[i], s_provider_badges.llm_base_y[i]);
            gfx_obj_set_visible(s_provider_badges.llm_labels[i], true);
        }
    }

    for (i = 0; i < EMOTE_IM_COUNT; ++i) {
        const bool configured = s_provider_badges.im_configured[i];
        const char *icon_name = configured ? s_im_logo_color[i] : s_im_logo_gray[i];
        uint32_t color = configured ? k_im_on : k_dim;
        gfx_coord_t y = s_provider_badges.im_base_y[i];

        if (i == s_provider_badges.active_im_index &&
                (now_ms - s_provider_badges.active_im_mark_ms) < 2500) {
            color = k_im_active;
            y += bounce_y_ofs;
        }

        if (s_provider_badges.im_icons[i] &&
                (!s_provider_badges.im_icon_ready[i] ||
                 s_provider_badges.im_icon_loaded_configured[i] != configured) &&
                emote_load_logo_to_dsc(icon_name, &s_provider_badges.im_icon_dsc[i])) {
            gfx_img_set_src(s_provider_badges.im_icons[i], &s_provider_badges.im_icon_dsc[i]);
            s_provider_badges.im_icon_loaded_configured[i] = configured;
            s_provider_badges.im_icon_ready[i] = true;
        }

        if (s_provider_badges.im_icons[i] && s_provider_badges.im_icon_ready[i]) {
            gfx_obj_set_pos(s_provider_badges.im_icons[i], s_provider_badges.im_base_x[i], y);
            gfx_obj_set_visible(s_provider_badges.im_icons[i], true);
            if (s_provider_badges.im_labels[i]) {
                gfx_obj_set_visible(s_provider_badges.im_labels[i], false);
            }
            continue;
        }

        s_provider_badges.im_icon_ready[i] = false;

        if (s_provider_badges.im_labels[i]) {
            gfx_label_set_color(s_provider_badges.im_labels[i], GFX_COLOR_HEX(color));
            gfx_obj_set_pos(s_provider_badges.im_labels[i], s_provider_badges.im_base_x[i], y);
            gfx_obj_set_visible(s_provider_badges.im_labels[i], true);
        }
    }
}

static void emote_create_provider_badges(void)
{
    static const char *llm_obj_names[EMOTE_LLM_COUNT] = {
        "llm_openai_badge", "llm_bailian_badge", "llm_deepseek_badge", "llm_anthropic_badge"
    };
    static const char *llm_icon_obj_names[EMOTE_LLM_COUNT] = {
        "llm_openai_logo", "llm_bailian_logo", "llm_deepseek_logo", "llm_anthropic_logo"
    };
    static const char *llm_texts[EMOTE_LLM_COUNT] = {
        "OPENAI", "QWEN", "DEEP", "CLAUDE"
    };
    static const char *im_obj_names[EMOTE_IM_COUNT] = {
        "im_wechat_badge", "im_qq_badge", "im_feishu_badge", "im_telegram_badge"
    };
    static const char *im_icon_obj_names[EMOTE_IM_COUNT] = {
        "im_wechat_logo", "im_qq_logo", "im_feishu_logo", "im_telegram_logo"
    };
    static const char *center_logo_obj_name = "center_claw_logo";
    static const char *clock_label_obj_name = "bottom_clock_label";
    static const char *im_texts[EMOTE_IM_COUNT] = {
        "WX", "QQ", "FS", "TG"
    };
    const int icon_size = EMOTE_PROVIDER_ICON_SIZE;
    int center_y;
    int row_gap;
    int llm_y;
    int im_y;
    int i;

    // `eye_anim` in layout is aligned to center with y=20, use the same occupied center line as anchor.
    center_y = (s_lcd_height / 2) + EMOTE_CENTER_OCCUPY_Y_OFFSET;
    row_gap = (s_lcd_height >= 240) ? 72 : 58;
    llm_y = center_y - row_gap - EMOTE_LLM_EXTRA_UP;
    im_y = center_y + row_gap;

    if (llm_y < 4) {
        llm_y = 4;
    }
    if (im_y > (s_lcd_height - icon_size)) {
        im_y = s_lcd_height - icon_size;
    }

    if (!s_emote_handle || s_provider_badges.created) {
        return;
    }

    s_provider_badges.center_logo = emote_create_obj_by_type(s_emote_handle, EMOTE_OBJ_TYPE_IMAGE, center_logo_obj_name);
    if (s_provider_badges.center_logo) {
        gfx_obj_set_visible(s_provider_badges.center_logo, false);
    }

    s_provider_badges.clock_label = emote_create_obj_by_type(s_emote_handle, EMOTE_OBJ_TYPE_LABEL, clock_label_obj_name);
    if (s_provider_badges.clock_label) {
        gfx_obj_set_visible(s_provider_badges.clock_label, true);
    }

    // Keep default eye/swim animation visible; provider rows are arranged around its occupied center area.
    emote_set_anim_visible(s_emote_handle, true);

    for (i = 0; i < EMOTE_LLM_COUNT; ++i) {
        gfx_obj_t *obj = emote_create_obj_by_type(s_emote_handle, EMOTE_OBJ_TYPE_LABEL, llm_obj_names[i]);
        gfx_obj_t *icon_obj = emote_create_obj_by_type(s_emote_handle, EMOTE_OBJ_TYPE_IMAGE, llm_icon_obj_names[i]);
        gfx_coord_t x = (gfx_coord_t)(((i + 1) * s_lcd_width) / (EMOTE_LLM_COUNT + 1) - (icon_size / 2));
        if (!obj) {
            ESP_LOGW(TAG, "create llm badge failed: %s", llm_obj_names[i]);
            continue;
        }

        s_provider_badges.llm_labels[i] = obj;
        s_provider_badges.llm_icons[i] = icon_obj;
        s_provider_badges.llm_base_x[i] = x;
        s_provider_badges.llm_base_y[i] = (gfx_coord_t)llm_y;

        gfx_obj_set_pos(obj, x, (gfx_coord_t)llm_y);
        gfx_label_set_text(obj, llm_texts[i]);
        gfx_label_set_bg_enable(obj, false);
        gfx_obj_set_visible(obj, true);
        if (icon_obj) {
            gfx_obj_set_pos(icon_obj, x, (gfx_coord_t)llm_y);
            gfx_obj_set_visible(icon_obj, false);
        }
    }

    for (i = 0; i < EMOTE_IM_COUNT; ++i) {
        gfx_obj_t *obj = emote_create_obj_by_type(s_emote_handle, EMOTE_OBJ_TYPE_LABEL, im_obj_names[i]);
        gfx_obj_t *icon_obj = emote_create_obj_by_type(s_emote_handle, EMOTE_OBJ_TYPE_IMAGE, im_icon_obj_names[i]);
        gfx_coord_t x = (gfx_coord_t)(((i + 1) * s_lcd_width) / (EMOTE_IM_COUNT + 1) - (icon_size / 2));
        if (!obj) {
            ESP_LOGW(TAG, "create im badge failed: %s", im_obj_names[i]);
            continue;
        }

        s_provider_badges.im_labels[i] = obj;
        s_provider_badges.im_icons[i] = icon_obj;
        s_provider_badges.im_base_x[i] = x;
        s_provider_badges.im_base_y[i] = (gfx_coord_t)im_y;

        gfx_obj_set_pos(obj, x, (gfx_coord_t)im_y);
        gfx_label_set_text(obj, im_texts[i]);
        gfx_label_set_bg_enable(obj, false);
        gfx_obj_set_visible(obj, true);
        if (icon_obj) {
            gfx_obj_set_pos(icon_obj, x, (gfx_coord_t)im_y);
            gfx_obj_set_visible(icon_obj, false);
        }
    }

    s_provider_badges.created = true;

    emote_lock(s_emote_handle);
    emote_update_provider_badges_locked();
    emote_unlock(s_emote_handle);
}

static bool emote_should_swap_color(const dev_display_lcd_config_t *lcd_cfg)
{
    if (lcd_cfg == NULL || lcd_cfg->sub_type == NULL) {
        return true;
    }

    if (strcmp(lcd_cfg->sub_type, "dsi") == 0 || strcmp(lcd_cfg->sub_type, "mipi_dsi") == 0 || strcmp(lcd_cfg->sub_type, "rgb") == 0) {
        return false;
    }

    return true;
}

static void emote_on_owner_changed(display_arbiter_owner_t owner, void *user_ctx)
{
    (void)user_ctx;

    if (owner != DISPLAY_ARBITER_OWNER_EMOTE || !s_emote_handle) {
        return;
    }

    esp_err_t err = emote_notify_all_refresh(s_emote_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "refresh after owner switch failed: %s", esp_err_to_name(err));
    }
}

static void emote_badge_anim_task_entry(void *arg)
{
    int64_t last_time_update_ms = 0;

    (void)arg;

    while (true) {
        bool need_refresh = false;
        int64_t now_ms;

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(EMOTE_BADGE_ANIM_PERIOD_MS));

        if (!s_emote_handle || !s_provider_badges.created) {
            continue;
        }

        now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_time_update_ms) >= EMOTE_TIME_UPDATE_MS) {
            need_refresh = true;
            last_time_update_ms = now_ms;
        }

        if (s_provider_badges.active_im_index >= 0) {
            int64_t elapsed_ms = now_ms - s_provider_badges.active_im_mark_ms;
            int64_t bounce_window_ms = (int64_t)EMOTE_IM_BOUNCE_STEP_MS * EMOTE_IM_BOUNCE_STEPS;
            int new_phase = 1;

            if (elapsed_ms < bounce_window_ms) {
                new_phase = 1 + (int)(elapsed_ms / EMOTE_IM_BOUNCE_STEP_MS);
            } else {
                new_phase = EMOTE_IM_BOUNCE_STEPS + 1;
            }

            if (elapsed_ms >= EMOTE_BADGE_ACTIVE_MS) {
                s_provider_badges.active_im_index = -1;
                new_phase = 0;
            }

            if (new_phase != s_provider_badges.im_anim_phase) {
                s_provider_badges.im_anim_phase = new_phase;
                need_refresh = true;
            }
        } else if (s_provider_badges.im_anim_phase != 0) {
            s_provider_badges.im_anim_phase = 0;
            need_refresh = true;
        }

        // If still within active window and no phase change, avoid heavy redraws.
        if (!need_refresh && s_provider_badges.active_im_index >= 0) {
            continue;
        }

        if (need_refresh) {
            emote_lock(s_emote_handle);
            emote_update_provider_badges_locked();
            emote_unlock(s_emote_handle);

            if (display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
                emote_notify_all_refresh(s_emote_handle);
            }
        }
    }
}

static void emote_flush_callback(int x_start, int y_start, int x_end, int y_end,
                                 const void *data, emote_handle_t handle)
{
    if (!s_panel_handle || !display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
        if (handle) {
            emote_notify_flush_finished(handle);
        }
        return;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel_handle, x_start, y_start, x_end, y_end, data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
    }

    if (handle) {
        emote_notify_flush_finished(handle);
    }
}

static void emote_update_callback(gfx_disp_event_t event, const void *obj,
                                  emote_handle_t handle)
{
    if (!handle) {
        return;
    }

    gfx_obj_t *wait_obj = emote_get_obj_by_name(handle, EMT_DEF_ELEM_EMERG_DLG);
    if (wait_obj == obj && event == GFX_DISP_EVENT_ALL_FRAME_DONE) {
        ESP_LOGI(TAG, "Emergency dialog finished");
    }
}

static esp_err_t emote_load_board_display(void)
{
#if !CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
    return ESP_ERR_NOT_SUPPORTED;
#else
    void *lcd_handle = NULL;
    void *lcd_config = NULL;
    esp_err_t err = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_config);
    if (err != ESP_OK) {
        return err;
    }

    dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)lcd_handle;
    dev_display_lcd_config_t *lcd_cfg = (dev_display_lcd_config_t *)lcd_config;

    ESP_RETURN_ON_FALSE(lcd_handles && lcd_cfg && lcd_handles->panel_handle,
                        ESP_ERR_INVALID_STATE, TAG, "display_lcd handle/config is NULL");

    s_panel_handle = lcd_handles->panel_handle;
    s_io_handle = lcd_handles->io_handle;
    s_lcd_width = lcd_cfg->lcd_width;
    s_lcd_height = lcd_cfg->lcd_height;
    return ESP_OK;
#endif
}

static emote_config_t emote_get_default_config(void)
{
    void *lcd_config = NULL;
    bool swap = true;
    if (esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_config) == ESP_OK) {
        swap = emote_should_swap_color((const dev_display_lcd_config_t *)lcd_config);
    }

    emote_config_t config = {
        .flags = {
            .swap = swap,
            .double_buffer = true,
            .buff_dma = true,
        },
        .gfx_emote = {
            .h_res = s_lcd_width,
            .v_res = s_lcd_height,
            .fps = 10,
        },
        .buffers = {
            .buf_pixels = (size_t)s_lcd_width * 16,
        },
        .task = {
            .task_priority = 3,
            .task_stack = 12 * 1024,
            .task_affinity = -1,
#ifdef CONFIG_SPIRAM_XIP_FROM_PSRAM
            .task_stack_in_ext = true,
#else
            .task_stack_in_ext = false,
#endif
        },
        .flush_cb = emote_flush_callback,
        .update_cb = emote_update_callback,
    };

    return config;
}

static esp_err_t emote_apply(const char *idle, const char *msg)
{
    ESP_RETURN_ON_FALSE(s_emote_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "emote handle is NULL");

    ESP_RETURN_ON_ERROR(emote_set_event_msg(s_emote_handle, EMOTE_MGR_EVT_SYS, msg), TAG, "set emote message failed");
    ESP_RETURN_ON_ERROR(emote_set_anim_emoji(s_emote_handle, idle), TAG, "set emote idle animation failed");

    emote_lock(s_emote_handle);
    emote_update_provider_badges_locked();
    emote_unlock(s_emote_handle);

    if (display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
        ESP_RETURN_ON_ERROR(emote_notify_all_refresh(s_emote_handle), TAG, "refresh emote display failed");
    }

    return ESP_OK;
}

esp_err_t emote_set_network_status(bool sta_connected, const char *ap_ssid)
{
    ESP_RETURN_ON_FALSE(s_emote_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "emote handle is NULL");

    const bool ap_present = (ap_ssid != NULL && ap_ssid[0] != '\0');
    const char *idle = sta_connected ? "swim" : "offline";

    char msg[112];
    if (sta_connected && ap_present) {
        snprintf(msg, sizeof(msg), "Online * AP: %s", ap_ssid);
    } else if (sta_connected) {
        snprintf(msg, sizeof(msg), "Wi-Fi connected");
    } else if (ap_present) {
        snprintf(msg, sizeof(msg), "Setup WiFi: %s", ap_ssid);
    } else {
        snprintf(msg, sizeof(msg), "Wi-Fi offline");
    }

    return emote_apply(idle, msg);
}

esp_err_t emote_set_provider_status(const char *llm_backend_type,
                                    const char *llm_base_url,
                                    const char *llm_model,
                                    const char *llm_api_key,
                                    const char *qq_app_id,
                                    const char *qq_app_secret,
                                    const char *feishu_app_id,
                                    const char *feishu_app_secret,
                                    const char *tg_bot_token,
                                    const char *wechat_token)
{
    int llm_index;
    int i;

    ESP_RETURN_ON_FALSE(s_emote_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "emote handle is NULL");

    for (i = 0; i < EMOTE_LLM_COUNT; ++i) {
        s_provider_badges.llm_configured[i] = false;
    }

    llm_index = emote_detect_llm_provider(llm_backend_type, llm_base_url, llm_model, llm_api_key);
    if (llm_index >= 0 && llm_index < EMOTE_LLM_COUNT) {
        s_provider_badges.llm_configured[llm_index] = true;
    }

    s_provider_badges.im_configured[EMOTE_IM_WECHAT] = emote_str_has_text(wechat_token);
    s_provider_badges.im_configured[EMOTE_IM_QQ] = emote_str_has_text(qq_app_id) && emote_str_has_text(qq_app_secret);
    s_provider_badges.im_configured[EMOTE_IM_FEISHU] = emote_str_has_text(feishu_app_id) && emote_str_has_text(feishu_app_secret);
    s_provider_badges.im_configured[EMOTE_IM_TELEGRAM] = emote_str_has_text(tg_bot_token);

    if (s_provider_badges.active_im_index >= 0 &&
            !s_provider_badges.im_configured[s_provider_badges.active_im_index]) {
        s_provider_badges.active_im_index = -1;
    }

    emote_lock(s_emote_handle);
    emote_update_provider_badges_locked();
    emote_unlock(s_emote_handle);

    if (display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
        emote_notify_all_refresh(s_emote_handle);
    }

    return ESP_OK;
}

esp_err_t emote_mark_active_im_platform(const char *platform)
{
    int im_index;

    ESP_RETURN_ON_FALSE(s_emote_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "emote handle is NULL");

    im_index = emote_im_platform_index(platform);
    if (im_index < 0 || im_index >= EMOTE_IM_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_provider_badges.active_im_index = im_index;
    s_provider_badges.active_im_mark_ms = esp_timer_get_time() / 1000;
    s_provider_badges.im_anim_phase = 0;

    if (!s_badge_anim_task) {
        if (xTaskCreate(emote_badge_anim_task_entry,
                        "emote_badge",
                        EMOTE_BADGE_ANIM_TASK_STACK,
                        NULL,
                        tskIDLE_PRIORITY + 2,
                        &s_badge_anim_task) != pdPASS) {
            s_badge_anim_task = NULL;
            ESP_LOGW(TAG, "create emote badge task failed");
        }
    }
    if (s_badge_anim_task) {
        xTaskNotifyGive(s_badge_anim_task);
    }

    emote_lock(s_emote_handle);
    emote_update_provider_badges_locked();
    emote_unlock(s_emote_handle);

    if (display_arbiter_is_owner(DISPLAY_ARBITER_OWNER_EMOTE)) {
        emote_notify_all_refresh(s_emote_handle);
    }

    return ESP_OK;
}

static void emote_cleanup(void)
{
    if (s_badge_anim_task) {
        vTaskDelete(s_badge_anim_task);
        s_badge_anim_task = NULL;
    }

    if (s_emote_handle) {
        emote_deinit(s_emote_handle);
        s_emote_handle = NULL;
    }
    display_arbiter_set_owner_changed_callback(NULL, NULL);
}

static esp_err_t emote_init_internal(void)
{
    emote_data_t data = {
        .type = EMOTE_SOURCE_PARTITION,
        .source = {
            .partition_label = EMOTE_ASSETS_PARTITION,
        },
        .flags = {
#ifdef CONFIG_SPIRAM_XIP_FROM_PSRAM
            .mmap_enable = false,
#else
            .mmap_enable = true,
#endif
        },
    };

    if (s_emote_handle) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(emote_load_board_display(), TAG, "Failed to get board display handles");

    emote_config_t config = emote_get_default_config();
    ESP_RETURN_ON_ERROR(display_arbiter_set_owner_changed_callback(emote_on_owner_changed, NULL), TAG, "register display owner callback failed");
    s_emote_handle = emote_init(&config);
    if (!s_emote_handle || !emote_is_initialized(s_emote_handle)) {
        emote_cleanup();
        return ESP_FAIL;
    }

    esp_err_t err = emote_mount_and_load_assets(s_emote_handle, &data);
    if (err != ESP_OK) {
        emote_cleanup();
        return err;
    }

    emote_create_provider_badges();

    if (!s_badge_anim_task) {
        if (xTaskCreate(emote_badge_anim_task_entry,
                        "emote_badge",
                        EMOTE_BADGE_ANIM_TASK_STACK,
                        NULL,
                        tskIDLE_PRIORITY + 2,
                        &s_badge_anim_task) != pdPASS) {
            s_badge_anim_task = NULL;
            ESP_LOGW(TAG, "create emote badge task failed");
        }
    }

    return emote_set_network_status(false, NULL);
}

esp_err_t emote_start(void)
{
    esp_err_t err = emote_init_internal();
    if (err != ESP_OK) {
        emote_cleanup();
    }
    return err;
}
