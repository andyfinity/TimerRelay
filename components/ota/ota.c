// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
#include "ota.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_log.h"

static const char *TAG = "ota";

static esp_ota_handle_t       s_handle;
static const esp_partition_t *s_part;
static size_t                 s_written;
static bool                   s_active;

void ota_mark_valid(void)
{
    // Only meaningful when rollback is enabled and we booted pending-verify.
    esp_ota_img_states_t st;
    const esp_partition_t *run = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "image confirmed valid; rollback cancelled");
        }
    }
}

esp_err_t ota_begin(void)
{
    if (s_active) return ESP_ERR_INVALID_STATE;

    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part) {
        ESP_LOGE(TAG, "no OTA partition available");
        return ESP_FAIL;
    }
    esp_err_t err = esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }
    s_written = 0;
    s_active = true;
    ESP_LOGI(TAG, "OTA started -> partition '%s'", s_part->label);
    return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t len)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        return err;
    }
    s_written += len;
    return ESP_OK;
}

esp_err_t ota_finish(void)
{
    if (!s_active) return ESP_ERR_INVALID_STATE;
    s_active = false;

    esp_err_t err = esp_ota_end(s_handle);   // validates the received image
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_ota_set_boot_partition(s_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "OTA complete (%u bytes); will boot '%s'",
             (unsigned)s_written, s_part->label);
    return ESP_OK;
}

void ota_abort(void)
{
    if (s_active) {
        esp_ota_abort(s_handle);
        s_active = false;
        ESP_LOGW(TAG, "OTA aborted after %u bytes", (unsigned)s_written);
    }
}

bool   ota_active(void)  { return s_active; }
size_t ota_written(void) { return s_written; }

static void reboot_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    ESP_LOGW(TAG, "rebooting to apply update");
    esp_restart();
}

void ota_reboot_soon(uint32_t delay_ms)
{
    xTaskCreate(reboot_task, "ota_reboot", 2560, (void *)(uintptr_t)delay_ms, 5, NULL);
}

const char *ota_running_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->version : "?";
}

const char *ota_running_partition(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run ? run->label : "?";
}
