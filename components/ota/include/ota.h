// SPDX-FileCopyrightText: 2026 Andy Russell
// SPDX-License-Identifier: Apache-2.0
// ota - streaming, push-style firmware update over the local web API.
//
// A client POSTs a compiled application .bin; the bytes are streamed straight
// into the inactive OTA slot (never buffered whole in RAM). On success the boot
// partition is switched and the device reboots. Rollback is enabled, so an image
// that fails to confirm healthy on next boot reverts automatically; app_main
// confirms health via ota_mark_valid() once startup completes.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Confirm the running image is healthy (cancels a pending rollback). Call once
// after startup has succeeded. No-op if not booted in pending-verify state.
void ota_mark_valid(void);

// Begin an update into the inactive slot. Fails if one is already in progress.
esp_err_t ota_begin(void);

// Stream a chunk of the incoming image. Must be preceded by ota_begin().
esp_err_t ota_write(const void *data, size_t len);

// Finalise: validate the image and set it as the boot partition. On ESP_OK the
// caller should send its HTTP response and then call ota_reboot_soon().
esp_err_t ota_finish(void);

// Abort an in-progress update and discard what was written.
void ota_abort(void);

// True while an update is being received.
bool   ota_active(void);
size_t ota_written(void);   // bytes written so far in the current update

// Reboot after `delay_ms` (gives the HTTP response time to flush).
void ota_reboot_soon(uint32_t delay_ms);

// Info about the currently running firmware.
const char *ota_running_version(void);
const char *ota_running_partition(void);

#ifdef __cplusplus
}
#endif
