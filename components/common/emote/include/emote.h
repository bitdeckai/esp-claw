/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t emote_start(void);
esp_err_t emote_set_network_status(bool sta_connected, const char *ap_ssid);
esp_err_t emote_set_provider_status(const char *llm_backend_type,
									const char *llm_base_url,
									const char *llm_model,
									const char *llm_api_key,
									const char *qq_app_id,
									const char *qq_app_secret,
									const char *feishu_app_id,
									const char *feishu_app_secret,
									const char *tg_bot_token,
									const char *wechat_token);
esp_err_t emote_mark_active_im_platform(const char *platform);

#ifdef __cplusplus
}
#endif
